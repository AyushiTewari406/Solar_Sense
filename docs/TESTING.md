# SolarSense — Testing Procedure

Two levels: **host tests** (no hardware, runs now) and **on-device tests**
(needs the ESP32). Do the host tests first — they catch build breaks in seconds
instead of after a two-minute upload.

---

## 0. Host tests — no ESP32 required

```bash
bash test/run_tests.sh
```

This compiles and links every firmware file (including `SolarSense.ino`)
against stub headers in `test/shim/`, then runs 26 assertions over the
power maths, the comparison guards, the JSON contract, the CSV contract, and
the upload ring buffer. Expected tail:

```
[ OK ] zero reference power does not divide by zero
[ OK ] missing reference panel blocks the comparison
[ OK ] saturated light reading withholds the comparison
[ OK ] buffer fills to capacity
[ OK ] overflow drops the oldest and counts it
[ OK ] pop advances oldest-first

0 failure(s)
```

Exit code is non-zero if anything fails. Run it after every edit.

**What this does not prove:** the shims are stubs. A passing run says the code
compiles and the logic is correct; it says nothing about whether a sensor is
wired properly. That is what section 2 onward is for.

---

## 1. Serial Monitor commands

Everything below is driven from the Arduino Serial Monitor at **115200 baud**.
Type a single character (no Enter needed if "No line ending" is selected):

| Key | Does |
|---|---|
| `h` | Command help |
| `i` | Scan both I2C buses, list every responding address |
| `r` | Force a sensor read and reprint the dashboard immediately |
| `j` | Print the exact JSON that would be POSTed |
| `c` | Print the CSV header and the latest row |
| `u` | Force a backend upload attempt now |
| `d` | Toggle demo view ↔ full diagnostic view |
| `t` | List DS18B20 ROM codes found on the OneWire bus |
| `s` | Reprint the sensor initialisation report |

---

## 2. BH1750 (light)

1. Press `i`. **Expect `0x23  BH1750 (light)` on `Wire`.**
   Not listed → check SDA GPIO21, SCL GPIO22, 3.3 V, GND.
2. Press `s`. Expect `BH1750 : OK`.
3. Press `r` in normal room light. Expect a few hundred to a few thousand lux.
4. Cover the sensor, press `r`. Value should collapse toward 0.
5. Point a phone torch at it, press `r`. Value should jump by an order of
   magnitude.

**Saturation check (outdoors, midday):** if `Light` shows
`>= 121556 lux [SATURATED]`, the sensor is at its ceiling. That is reported
honestly rather than passed off as a real number. `BH1750_MTREG` in `config.h`
is already set to 31 to push the ceiling from ~54,600 lux to ~121,500 — the
default setting would saturate in ordinary Chennai sunlight.

---

## 3. INA219 Panel A

1. Press `i`. **Expect `0x40 INA219 (no bridge)` on `Wire`.**
2. Press `s`. Expect `INA219 Panel A : OK`.
3. With nothing attached to Vin+/Vin−, expect roughly 0 V and 0 A — a real
   zero, shown as `0.00 V`, not `---`.
4. Attach the panel. In sunlight expect a plausible voltage (a 20 W 18 V panel
   reads ~15–21 V open circuit) and a current that tracks shading.
5. Shade the panel with your hand, press `r`. Current should drop sharply.
6. **Unplug the sensor's SDA wire and press `r`.** Expect
   `Voltage : ERROR (read failed)` — not a stale value and not a zero. This is
   the disconnection-detection path; it re-probes the address every cycle.

---

## 4. INA219 Panel B

Identical to Panel A, except press `i` and look at the **`Wire1`** scan:
**expect `0x40 INA219 (no bridge)` on `Wire1` (SDA 16 / SCL 17)**.

Panel A and Panel B both at 0x40 on *different* buses is correct and expected.
Two devices at 0x40 on the *same* bus is the failure this layout avoids.

---

## 5. INA219 Panel C

With the current hardware, the expected result is
**`INA219 Panel C : NOT CONNECTED`**, and Panel C's fields showing `---`.
That is correct, not a bug. Two ways to go further:

**a) Bench-test C alone.** In `config.h` set `PANEL_A_ENABLED` and
`PANEL_B_ENABLED` to `false`, `PANEL_C_ENABLED` to `true`, `PANEL_C_BUS` to
`BUS_WIRE`, `PANEL_C_ADDRESS` to `0x40`. Wire C to GPIO21/22 and re-upload.
Then run section 3's steps against Panel C. Revert afterwards.

**b) After soldering the address pads.** Bridge A0 on board B and A1 on board
C, move all three to GPIO21/22, set `INA219_ADDRESSES_SOLDERED true`, re-upload.
Press `i`: **expect 0x40, 0x41 and 0x44 all on `Wire`.** Then `s` should show
all three panels `OK` simultaneously — that is the milestone that closes this
hardware gap.

---

## 6. WiFi

1. Copy `SolarSense/secrets.h.example` to `SolarSense/secrets.h`, fill in SSID
   and password, re-upload.
2. At boot expect:
   ```
   Connecting to WiFi.....
   WiFi connected. IP address: 192.168.1.42 (-54 dBm)
   Clock synchronised over NTP.
   ```
3. `Timestamp` in the dashboard should now show a real date with `[NTP]`.
4. **Failure test — the important one.** Power off the router (or use a wrong
   password). Confirm:
   - Boot still completes; it does not hang.
   - Sensor readings keep updating every 5 s.
   - `WiFi : DISCONNECTED (retrying, acquisition unaffected)`.
   - Timestamps fall back to `T+HH:MM:SS (no clock) [UPTIME]`.

   WiFi failure must never stop acquisition. If the dashboard stops updating,
   that is a real bug.

---

## 7. JSON generation

Press `j`. Expect a single line of JSON matching
[BACKEND_API.md](BACKEND_API.md). Check specifically:

- Panel C serialises as `"voltage":null`, **not** `"voltage":0`.
- `"status":"NOT CONNECTED"` accompanies those nulls.
- `"simulated":false`.
- Byte count is reported and is well under 1400.

Paste it into any JSON validator; it must parse.

---

## 8. Backend upload

Without a backend yet, stand one up in ten seconds:

```bash
python -m http.server 8000
```

Point `SS_BACKEND_URL` at `http://<your-pc-ip>:8000/` and re-upload. Python's
server returns 501 for POST, which is *not* 2xx — so this actually tests the
**failure** path, which is the more valuable one:

```
Backend upload failed (HTTP ERROR, http=501)
Saving data locally... (1 buffered, Serial CSV only)
```

For the success path, a three-line Flask app:

```python
from flask import Flask, request
app = Flask(__name__)

@app.post("/api/sensors/data")
def ingest():
    print(request.get_json())
    return "", 201

app.run(host="0.0.0.0", port=8000)
```

Then press `u` and expect `Result: SENT (http=201)`, with the dashboard showing
`Backend : SENT http=201 ok=1 fail=0`.

**Buffer test.** Stop the server, wait two minutes (4 upload cycles), watch
`Upload buffer` climb to 4 pending. Restart the server, press `u`, and confirm
the backlog drains oldest-first and `pending` returns to 0.

---

## 9. Data logging

- **Without microSD:** CSV rows appear on Serial every 5 s under the header
  printed at boot. Select them in the Serial Monitor, paste into a `.csv`, and
  they open directly in Excel or `pandas.read_csv`.
- **With microSD:** expect `microSD : OK` at boot and
  `SD Card : OK  N rows written` on the dashboard. Pull the card and confirm
  `/solarsense.csv` has a header plus one row per 5 s.
- Press `c` any time to print the header and the latest row together.

Confirm that columns for absent sensors are **empty**, not `0`.

---

## 10. Error handling

Each of these should degrade, not crash. The station must still be running
5 seconds later.

| Test | Expected |
|---|---|
| Unplug BH1750's SDA mid-run | `Light : ERROR (read failed)`, panels keep reading |
| Unplug INA219 A mid-run | Panel A `ERROR (read failed)`; comparison switches to `reference panel A unavailable` |
| Run indoors under 20,000 lux | Comparison withheld: `low sunlight - difference not attributable to soiling` |
| Cover all panels completely | `reference output too low to compare` — no division by zero |
| Wrong WiFi password | Boot completes, acquisition continues, retries in background |
| No microSD | `NOT CONNECTED`, CSV falls back to Serial |
| No DS3231 | `WARNING`-free fallback to NTP, or to uptime if there is no WiFi either |

---

## 11. Expected Serial Monitor output

Below is real serialiser output from the host harness, using representative
values for the hardware that exists today (BH1750 + INA219 A + INA219 B, Panel
C and all future sensors absent). This is what a correct upload looks like.

### Boot

```
==================================================
                 SOLARSENSE
     Solar panel soiling monitoring station
==================================================
Device          : SOLARSENSE_01
Firmware        : 0.5.0

I2C scan on Wire  (SDA 21 / SCL 22):
  0x23  BH1750 (light)
  0x40  INA219 (no bridge)
I2C scan on Wire1 (SDA 16 / SCL 17):
  0x40  INA219 (no bridge)

Connecting to WiFi.....
WiFi connected. IP address: 192.168.1.42 (-54 dBm)
Clock synchronised over NTP.

SENSOR INITIALISATION
---------------------
BH1750             : OK
INA219 Panel A     : OK
INA219 Panel B     : OK
INA219 Panel C     : NOT CONNECTED
DS3231             : NOT CONNECTED
DS18B20            : NOT CONNECTED
DHT22              : NOT CONNECTED
microSD            : NOT CONNECTED
Rain gauge         : NOT CONNECTED
WiFi               : CONNECTED
Backend            : CONFIGURED
Time source        : NTP

NOTE: Panel C is disabled in config.h. All three INA219
      boards are still at address 0x40, so C cannot share
      a bus with A or B. Bridge A0 on B and A1 on C, then
      set INA219_ADDRESSES_SOLDERED true.
```

### Every 5 seconds — full diagnostic view

```
==================================================
                 SOLARSENSE
==================================================

Timestamp       : 2026-08-20 21:30:15  [NTP]
Sequence        : 42

ENVIRONMENT
Light           : 82450 lux
Ambient Temp    : ---
Humidity        : ---
Rainfall        : ---

PANEL A - CLEAN REFERENCE
Voltage         : 18.42 V
Current         : 1.240 A
Power           : 22.84 W
Temperature     : ---

PANEL B - NEVER CLEANED
Voltage         : 17.91 V
Current         : 0.910 A
Power           : 16.30 W
Temperature     : ---

PANEL C - WEEKLY CLEANED
Voltage         : ---
Current         : ---
Power           : ---
Temperature     : ---

COMPARISON
Panel B Loss    : 28.6 %  (efficiency 0.714)
Panel C Loss    : --- (panel C unavailable)
Note            : relative power only; not yet a soiling diagnosis

SYSTEM
WiFi            : CONNECTED  192.168.1.42  (-54 dBm)
Backend         : SENT  http=201  ok=7 fail=0
SD Card         : NOT CONNECTED (CSV echoed to Serial instead)
Upload buffer   : 0 pending, 0 dropped
Free heap       : 241536 bytes
==================================================
```

### Demo view (press `d`, or set `DEMO_MODE true`)

```
==================================================
            SOLARSENSE LIVE MONITOR
==================================================

Sunlight        : 82.5 klux
Panel A (clean) : 22.84 W
Panel B (never) : 16.30 W
Panel C (weekly): ---

Estimated Loss vs Clean Reference
Never Cleaned   : 28.6 %
Weekly Cleaned  : ---

System Status   : ONLINE
==================================================
```
