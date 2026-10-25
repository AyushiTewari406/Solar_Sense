# SolarSense — Wiring

Two tables. The first is what the firmware talks to **today**. The second is
what it is **built to talk to** and currently reports as `NOT CONNECTED`.

All modules run from the ESP32 3.3 V and GND rails unless noted.

---

## CONNECTED NOW

| Module | ESP32 pins | Bus | I2C address | Firmware state |
|---|---|---|---|---|
| BH1750 (light) | SDA → GPIO21, SCL → GPIO22 | `Wire` | 0x23 | Enabled |
| INA219 — Panel A (clean reference) | SDA → GPIO21, SCL → GPIO22 | `Wire` | 0x40 | Enabled |
| INA219 — Panel B (never cleaned) | SDA → GPIO16, SCL → GPIO17 | `Wire1` | 0x40 | Enabled |
| INA219 — Panel C (weekly cleaned) | tested standalone only | — | 0x40 | **Disabled** in `config.h` |

### Why two I2C buses

All three INA219 boards ship at address 0x40 and the address pads (A0/A1) have
not been bridged. Two devices at the same address on the same bus is not a
software problem — the ESP32 physically cannot distinguish them. Putting Panel
B on a second hardware I2C bus (`Wire1`, GPIO16/17) sidesteps the collision for
two panels. There is no third hardware I2C peripheral on the ESP32, so Panel C
has nowhere to go until the pads are bridged.

The firmware states this rather than hiding it: Panel C reports
`NOT CONNECTED`, its JSON fields serialise as `null`, and no Panel C loss
figure is produced.

### Bench-testing Panel C on its own

In `config.h`:

```c
#define PANEL_A_ENABLED false
#define PANEL_B_ENABLED false
#define PANEL_C_ENABLED true
#define PANEL_C_BUS     BUS_WIRE
#define PANEL_C_ADDRESS 0x40
```

---

## FUTURE HARDWARE — designed in, not yet wired

The firmware initialises each of these, reports `NOT CONNECTED` when it is
absent, and carries on. Nothing below needs a code change to come online —
only the physical wiring, and for the DS18B20s one paste of ROM codes.

| Module | ESP32 pins | Notes |
|---|---|---|
| DS3231 RTC | SDA → GPIO21, SCL → GPIO22 | `Wire`, address 0x68. Until it arrives, NTP over WiFi supplies real timestamps; the record says which source answered. |
| DS18B20 ×3 (panel temperature) | all DATA → GPIO4 | Shared OneWire bus. **Needs one 4.7 kΩ pull-up from DATA to 3.3 V** — the bus will not enumerate without it. |
| DHT22 (ambient temp + humidity) | DATA → GPIO15 | |
| microSD | MOSI → GPIO23, MISO → GPIO19, SCK → GPIO18, CS → GPIO5 | ESP32 default VSPI pins. Until present, CSV rows are echoed to Serial. |
| Rain gauge (tipping bucket) | DATA → GPIO27 (provisional) | Disabled by default; pin not yet chosen. Interrupt-driven with a 120 ms debounce. |

### DS18B20 panel mapping

Three probes on one bus enumerate in an arbitrary but stable order. If you do
not tell the firmware which ROM belongs to which panel, **panel readings can be
silently swapped** — a hot Panel B reported as Panel A would corrupt the whole
thermal correction later.

The firmware prints the discovered ROM codes at boot (or press `t` in the
Serial Monitor) and warns while the mapping is unset. To fix it: warm one probe
with your fingers, see which index moves, and paste its ROM into the matching
entry in `config.h`:

```c
static const uint8_t DS18B20_ROM_PANEL_A[8] = {0x28, 0xFF, 0x64, 0x1E, 0x0C, 0x1A, 0x01, 0x3C};
```

---

## After soldering the INA219 address pads

The permanent fix: bridge **A0 on Panel B's board** and **A1 on Panel C's
board**. That gives 0x40 / 0x41 / 0x44, all three on `Wire`, and GPIO16/17 come
free.

One line in `config.h` migrates the firmware:

```c
#define INA219_ADDRESSES_SOLDERED true
```

Rewire all three boards to SDA GPIO21 / SCL GPIO22, re-upload, and confirm with
the `i` command that the scan shows 0x40, 0x41 and 0x44 on `Wire`.
