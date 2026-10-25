import os
import sys
import time

import requests

url = (sys.argv[1] if len(sys.argv) > 1 else os.environ.get("SOLARSENSE_URL", "http://localhost:3000")).rstrip("/")
timeout_s = float(sys.argv[2]) if len(sys.argv) > 2 else 60.0
probe = url + "/api/sensors/latest"
deadline = time.monotonic() + timeout_s

while True:
    try:
        resp = requests.get(probe, timeout=3)
        if resp.ok and "device_id" in resp.json():
            print(f"SolarSense is ready at {url}")
            sys.exit(0)
        last = f"HTTP {resp.status_code}"
    except (requests.RequestException, ValueError) as exc:
        last = type(exc).__name__
    if time.monotonic() > deadline:
        print(f"SolarSense did not become ready at {probe} within {timeout_s:.0f}s (last: {last})")
        sys.exit(1)
    time.sleep(1)
