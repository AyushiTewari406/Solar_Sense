import json
import math
import os
import statistics
import threading
import time
from concurrent.futures import ThreadPoolExecutor

import pytest
import requests

MAX_SAFE_CONCURRENCY = 50
MAX_SAFE_REQUESTS = 2000

BASE_URL = os.environ.get("SOLARSENSE_URL", "http://localhost:3000").rstrip("/")
CONCURRENCY = int(os.environ.get("PERF_CONCURRENCY", "5"))
REQUESTS_PER_ENDPOINT = int(os.environ.get("PERF_REQUESTS", "100"))
TIMEOUT_S = float(os.environ.get("PERF_TIMEOUT_S", "10"))
MAX_AVG_MS = float(os.environ.get("PERF_MAX_AVG_MS", "200"))
MAX_P95_MS = float(os.environ.get("PERF_MAX_P95_MS", "500"))
MAX_MS = float(os.environ.get("PERF_MAX_MS", "2000"))
MIN_SUCCESS_RATE = float(os.environ.get("PERF_MIN_SUCCESS_RATE", "0.99"))
ENDPOINTS = [p.strip() for p in os.environ.get(
    "PERF_ENDPOINTS", "/,/api/sensors/latest,/api/sensors/history").split(",") if p.strip()]

_thread_local = threading.local()


def _session():
    if not hasattr(_thread_local, "session"):
        _thread_local.session = requests.Session()
    return _thread_local.session


def _timed_get(url):
    start = time.perf_counter()
    try:
        resp = _session().get(url, timeout=TIMEOUT_S)
        _ = resp.content
        return (time.perf_counter() - start) * 1000.0, resp.status_code, None
    except requests.RequestException as exc:
        return (time.perf_counter() - start) * 1000.0, None, type(exc).__name__


def percentile(values, pct):
    ordered = sorted(values)
    rank = max(1, math.ceil(pct / 100.0 * len(ordered)))
    return ordered[rank - 1]


def run_load(url, total, concurrency):
    with ThreadPoolExecutor(max_workers=concurrency) as pool:
        results = list(pool.map(_timed_get, [url] * total))
    times = [r[0] for r in results]
    ok = [r for r in results if r[1] is not None and 200 <= r[1] < 300]
    failures = {}
    for _, status, err in results:
        if status is None or not 200 <= status < 300:
            key = err or f"HTTP {status}"
            failures[key] = failures.get(key, 0) + 1
    return {
        "url": url,
        "requests": total,
        "concurrency": concurrency,
        "successes": len(ok),
        "success_rate": len(ok) / total,
        "avg_ms": statistics.fmean(times),
        "p95_ms": percentile(times, 95),
        "max_ms": max(times),
        "min_ms": min(times),
        "failures": failures,
    }


@pytest.fixture(scope="module", autouse=True)
def _guard_and_check_server(reports_dir):
    if not 1 <= CONCURRENCY <= MAX_SAFE_CONCURRENCY:
        pytest.exit(f"PERF_CONCURRENCY must be 1..{MAX_SAFE_CONCURRENCY} (got {CONCURRENCY})", 2)
    if not 1 <= REQUESTS_PER_ENDPOINT <= MAX_SAFE_REQUESTS:
        pytest.exit(f"PERF_REQUESTS must be 1..{MAX_SAFE_REQUESTS} (got {REQUESTS_PER_ENDPOINT})", 2)
    try:
        requests.get(BASE_URL + "/api/sensors/latest", timeout=TIMEOUT_S).raise_for_status()
    except requests.RequestException as exc:
        pytest.fail(f"SolarSense server not reachable at {BASE_URL}: {exc}", pytrace=False)
    summary = []
    yield summary
    out = reports_dir / "performance_summary.json"
    out.write_text(json.dumps({
        "base_url": BASE_URL,
        "thresholds": {"max_avg_ms": MAX_AVG_MS, "max_p95_ms": MAX_P95_MS,
                       "max_ms": MAX_MS, "min_success_rate": MIN_SUCCESS_RATE},
        "results": summary,
    }, indent=2), encoding="utf-8")


@pytest.mark.performance
@pytest.mark.parametrize("path", ENDPOINTS)
def test_endpoint_response_time(path, _guard_and_check_server, record_property):
    url = BASE_URL + path
    _timed_get(url)
    stats = run_load(url, REQUESTS_PER_ENDPOINT, CONCURRENCY)
    _guard_and_check_server.append(stats)

    for key in ("requests", "concurrency", "success_rate", "avg_ms", "p95_ms", "max_ms"):
        record_property(key, round(stats[key], 3) if isinstance(stats[key], float) else stats[key])

    print(f"\n{path}: {stats['requests']} requests @ concurrency {stats['concurrency']}"
          f" | success {stats['success_rate']:.1%}"
          f" | avg {stats['avg_ms']:.1f} ms | p95 {stats['p95_ms']:.1f} ms"
          f" | max {stats['max_ms']:.1f} ms | min {stats['min_ms']:.1f} ms"
          + (f" | failures {stats['failures']}" if stats["failures"] else ""))

    problems = []
    if stats["success_rate"] < MIN_SUCCESS_RATE:
        problems.append(f"success rate {stats['success_rate']:.2%} < {MIN_SUCCESS_RATE:.2%} {stats['failures']}")
    if stats["avg_ms"] > MAX_AVG_MS:
        problems.append(f"avg {stats['avg_ms']:.1f} ms > {MAX_AVG_MS} ms")
    if stats["p95_ms"] > MAX_P95_MS:
        problems.append(f"p95 {stats['p95_ms']:.1f} ms > {MAX_P95_MS} ms")
    if stats["max_ms"] > MAX_MS:
        problems.append(f"max {stats['max_ms']:.1f} ms > {MAX_MS} ms")
    assert not problems, f"{path} exceeded thresholds: " + "; ".join(problems)
