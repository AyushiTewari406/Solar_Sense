# SolarSense — Firmware → Backend API Specification

This is the contract between the ESP32 field node and the backend. A teammate
can build the receiving service against this document without reading any
firmware code.

Every example below is **actual output** from the firmware's serialiser, not a
hand-written mock — see [`test/main.cpp`](test/main.cpp), which asserts on it.

---

## Endpoint

```
POST /api/sensors/data
Content-Type: application/json
X-API-Key: <per-node key>      # only sent when configured; omit until implemented
```

The full URL is configured on the device in `SolarSense/secrets.h`
(`SS_BACKEND_URL`). The firmware does not assume a host, port, or path.

### Response contract

| Backend returns | Firmware behaviour |
|---|---|
| `2xx` | Record considered delivered. Dropped from the retry buffer. |
| anything else | Treated as **not delivered**. Record is buffered in RAM and retried. |
| no response within 4 s | Same as above — timeout, buffer, retry. |

**This is deliberate and it matters.** The firmware only stops retrying on an
explicit 2xx. If the backend acknowledges on receipt but then fails to write to
the database, that record is lost with no trace. Send the 2xx *after* the
durable write, not before. (This is the same guarantee the ROS 2 backend plan
calls FR-45.)

Response body is ignored. `201 Created` is the natural choice.

---

## Request schema

```json
{
  "device_id": "SOLARSENSE_01",
  "seq": 42,
  "timestamp": "2026-08-20T21:30:15",
  "time_source": "NTP",
  "uptime_ms": 3615000,
  "light_lux": 82450.0,
  "light_status": "OK",
  "panelA": {
    "voltage": 18.42,
    "current": 1.240,
    "power": 22.84,
    "temperature": null,
    "status": "OK",
    "temperature_status": "NOT CONNECTED"
  },
  "panelB": {
    "voltage": 17.91,
    "current": 0.910,
    "power": 16.30,
    "temperature": null,
    "status": "OK",
    "temperature_status": "NOT CONNECTED"
  },
  "panelC": {
    "voltage": null,
    "current": null,
    "power": null,
    "temperature": null,
    "status": "NOT CONNECTED",
    "temperature_status": "NOT CONNECTED"
  },
  "ambient": {
    "temperature": null,
    "humidity": null,
    "rainfall_mm": null,
    "rainfall_total_mm": null,
    "status": "NOT CONNECTED"
  },
  "comparison": {
    "valid": true,
    "note": "valid",
    "efficiency_B": 0.7136,
    "efficiency_C": null,
    "loss_B_pct": 28.64,
    "loss_C_pct": null
  },
  "firmware": "0.5.0",
  "simulated": false
}
```

Typical size: **816 bytes**. Hard ceiling 1400 bytes (the firmware refuses to
send a truncated payload).

### Field reference

| Field | Type | Meaning |
|---|---|---|
| `device_id` | string | Station identity. One per physical node. |
| `seq` | integer | Monotonic per boot, starting at 1. **Resets to 1 on reboot** — see dedup note below. |
| `timestamp` | string | ISO-8601 local time, or `"uptime:<seconds>"` when no clock exists. Always check `time_source` before parsing. |
| `time_source` | string | `DS3231` \| `NTP` \| `UPTIME`. |
| `uptime_ms` | integer | Milliseconds since boot. Orders records even with no clock. |
| `light_lux` | number \| null | Irradiance proxy. |
| `light_status` | string | See status values below. `SATURATED` means true irradiance was *at least* this. |
| `panelA/B/C.voltage` | number \| null | Volts, panel terminal (bus + shunt). |
| `panelA/B/C.current` | number \| null | Amps. |
| `panelA/B/C.power` | number \| null | Watts, computed as voltage × current. |
| `panelA/B/C.temperature` | number \| null | °C, back-of-module. |
| `panelA/B/C.status` | string | Status of the electrical reading. |
| `panelA/B/C.temperature_status` | string | Status of the thermal reading, independent of the electrical one. |
| `ambient.*` | number \| null | Station weather context. |
| `comparison.valid` | boolean | Whether a soiling-loss figure was produced at all. |
| `comparison.note` | string | Why, when `valid` is false. |
| `comparison.efficiency_B/C` | number \| null | Panel power ÷ Panel A power. 1.0 = matches reference. |
| `comparison.loss_B_pct/loss_C_pct` | number \| null | `100 × (1 − efficiency)`. |
| `firmware` | string | Firmware version that produced the record. |
| `simulated` | boolean | **`true` means one or more values were fabricated.** Reject or quarantine these. |

### Status values

| Value | Meaning |
|---|---|
| `OK` | Fresh, trustworthy reading. |
| `NOT CONNECTED` | Sensor absent or disabled. Value is `null`. |
| `READ ERROR` | Sensor present at boot but this read failed. Value is `null`. |
| `SATURATED` | Reading is at the sensor's ceiling; the true value is ≥ this. |
| `SIMULATED` | Fabricated by demo simulation mode. Not a measurement. |

### The null rule

**A missing measurement is always `null`, never `0`.** A panel producing zero
watts at night and a panel whose sensor is unplugged are different facts, and
conflating them would put fake zeros into the soiling model's training data.
Store `NULL`, not `0`, and key the distinction off `status`.

---

## Notes for the backend implementer

**Deduplication.** `(device_id, seq)` is unique *within a boot session*. `seq`
restarts at 1 after a reset, so it is not globally unique on its own. Two
options: key on `(device_id, seq, uptime_ms)`, or — better — assign a
server-side received-at timestamp and treat `seq` as ordering-within-session
only. A DS3231 will make `timestamp` reliable enough to key on; until then it
may be `uptime:...`.

**Retry storms.** After an outage the firmware flushes its backlog oldest-first
at up to 5 records per 30 s cycle, so it will not burst. Buffer capacity is 60
records (~30 minutes); older ones are dropped and the drop count is shown on
the device's serial dashboard.

**`comparison` is a convenience, not an authority.** The firmware computes
relative power loss so the serial demo and the dashboard cannot disagree. It is
raw relative power with guards applied, **not** a soiling diagnosis — it is not
corrected for panel temperature, spectral response, angle, or panel mismatch.
The backend/AI layer owns the actual soiling determination. Recompute from raw
values if you prefer; the inputs are all in the payload.

**When `comparison.valid` is false**, `note` carries the reason:

| Note | Cause |
|---|---|
| `reference panel A unavailable` | No reading from the clean reference. |
| `reference output too low to compare` | Panel A below 0.5 W — ratio would be meaningless, division unsafe. |
| `no light reading - cannot separate cloud from soiling` | Light sensor absent or failed. |
| `low sunlight - difference not attributable to soiling` | Below 20,000 lux. |

---

## Alignment with the existing ROS 2 backend scaffold

The `solarsense_ws` scaffold's `ss_ingest_node` expects a different shape
(SRS §4.3): a batch envelope with `node_id`, a `records` array, and short-circuit
current fields (`isc_a_ma`). **These two schemas do not currently match.** One
side must adapt before integration. Suggested mapping if the backend adapts:

| ROS `RawRecord.msg` | This payload |
|---|---|
| `node_id` | `device_id` |
| `seq` | `seq` |
| `ts` | `timestamp` (+ `time_source`) |
| `isc_a_ma` / `isc_b_ma` / `isc_c_ma` | `panelA/B/C.current` × 1000 |
| — (not in msg) | `panelA/B/C.voltage`, `.power` — extra, and worth keeping: current alone cannot give power |
| `tbom_a_c` / `tbom_b_c` / `tbom_c_c` | `panelA/B/C.temperature` |
| `rh_pct` | `ambient.humidity` |
| `ambient_c` | `ambient.temperature` |
| `lux` | `light_lux` |
| `rain_tips` | derive from `ambient.rainfall_total_mm` ÷ 0.2794 |
| `batt_v` | not yet sent — no battery monitor on the node |
| `flags` | replace with the per-field `status` strings, which carry strictly more information |

Two differences worth an explicit decision:

1. **Single record vs batch.** This firmware posts one record per request. The
   scaffold's envelope is a batch. Batching is the better long-term choice for
   the 30-day-offline story; say the word and the firmware can send
   `{"device_id": ..., "records": [...]}` instead — the retry buffer already
   holds records in the right order for it.
2. **Ack body.** The scaffold returns `{"accepted_seq": [...], "duplicate_seq":
   [...], "committed": true}`. This firmware currently only reads the HTTP
   status code. Parsing that body would let it drop exactly the accepted
   records instead of assuming the whole request landed.

---

## CSV schema (local log / microSD backup)

Same data, flat, one row per sample. Written to `/solarsense.csv` on the
microSD when present, echoed to Serial when not.

```csv
timestamp,time_source,seq,light_lux,light_status,panelA_voltage,panelA_current,panelA_power,panelA_status,panelB_voltage,panelB_current,panelB_power,panelB_status,panelC_voltage,panelC_current,panelC_power,panelC_status,panelA_temp,panelB_temp,panelC_temp,ambient_temp,humidity,rainfall_mm,loss_B_pct,loss_C_pct,comparison_valid,simulated
2026-08-20 21:30:15,NTP,42,82450.0,OK,18.420,1.2400,22.841,OK,17.910,0.9100,16.298,OK,,,,NOT CONNECTED,,,,,,,28.64,,1,0
```

Missing values are **empty fields**, which `pandas.read_csv` reads as `NaN`.
Writing `0` there would create fake measurements.
