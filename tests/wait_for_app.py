"""Block until the SolarSense dashboard server answers, or exit non-zero.

Usage: python tests/wait_for_app.py [base_url] [timeout_seconds] [--pid-file app.pid]
Defaults: $SOLARSENSE_URL or http://localhost:3000, 60 seconds.

With --pid-file, also watch the server process the pipeline started and fail
as soon as it exits (e.g. a crash on startup) instead of waiting out the timeout.
"""

import argparse
import os
import sys
import time

import requests


def process_alive(pid):
    if os.name == "nt":
        import ctypes
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return False
        code = ctypes.c_ulong()
        kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
        kernel32.CloseHandle(handle)
        return code.value == 259  # STILL_ACTIVE
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


parser = argparse.ArgumentParser()
parser.add_argument("url", nargs="?", default=os.environ.get("SOLARSENSE_URL", "http://localhost:3000"))
parser.add_argument("timeout", nargs="?", type=float, default=60.0)
parser.add_argument("--pid-file")
args = parser.parse_args()

url = args.url.rstrip("/")
probe = url + "/api/sensors/latest"
pid = int(open(args.pid_file).read().strip()) if args.pid_file else None
deadline = time.monotonic() + args.timeout

while True:
    try:
        resp = requests.get(probe, timeout=3)
        if resp.ok and "device_id" in resp.json():
            print(f"SolarSense is ready at {url}")
            sys.exit(0)
        last = f"HTTP {resp.status_code}"
    except (requests.RequestException, ValueError) as exc:
        last = type(exc).__name__
    if pid is not None and not process_alive(pid):
        print(f"SolarSense server process {pid} exited before {probe} became reachable (last: {last})")
        sys.exit(1)
    if time.monotonic() > deadline:
        print(f"SolarSense did not become ready at {probe} within {args.timeout:.0f}s (last: {last})")
        sys.exit(1)
    time.sleep(1)
