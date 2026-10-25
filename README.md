# SolarSense

## What it does

Dust on a solar panel cuts its output, but so do clouds and heat, so looking at
power alone does not tell you when a panel needs cleaning. SolarSense runs three
identical panels side by side:

| Panel | How it is treated | Why |
|---|---|---|
| A | Always kept clean | Reference for what the panels should produce |
| B | Never cleaned | Shows the worst case |
| C | Cleaned once a week | The usual maintenance schedule, for comparison |

An ESP32 reads sunlight (BH1750) and the voltage and current of each panel
(INA219). When there is enough sunlight, the drop in B and C compared to A is
put down to soiling. The dashboard splits each panel into a 4 x 3 grid of
zones, picks the dirtiest zone and, when the decision is "clean now", sends the
robotic arm to brush that zone. The arm is a 6-DOF arm on an Arduino Uno with a
PCA9685 servo driver and an HC-SR04 ultrasonic sensor.

## Repository layout

```
SolarSense/                  ESP32 field node sketch (Arduino IDE)
  SolarSense.ino
  config.h                   pins, intervals, I2C addresses, thresholds
  secrets.h.example          copy to secrets.h and add your WiFi details
  src/                       sensors, data, logging, communication, ui
dashboard/                   Node.js + Express backend and the web dashboard
  server.js
  index.html
robotic_arm/
  firmware/robotic_arm/      Arduino Uno sketch for the arm
  server/                    FastAPI arm controller (serial, kinematics, cleaning)
  frontend/                  arm control panel, 3D view, pick and drop, QR scanner
  scripts/                   start the arm controller, flash the Uno
test/                        host-side C++ tests for the firmware logic
tests/                       Selenium functional test and HTTP performance test
docs/                        wiring, backend API, testing notes, arm notes
Jenkinsfile                  CI pipeline that runs both test suites
```

## How the two halves talk

```
ESP32 --(WiFi, JSON POST)--> dashboard/server.js :3000 --(HTTP)--> robotic_arm/server :8000 --(USB serial)--> Arduino Uno + PCA9685
                                   ^                                        |
                                   +------ clean_status polling ------------+
```

1. Pressing "Actuate robotic arm now" on the dashboard posts the target zone to
   `/api/actuate/clean`.
2. `server.js` forwards it to the arm controller at `ARM_URL`
   (default `http://localhost:8000`) on `/api/routine/clean_zone`.
3. The arm controller maps the zone position on the 1.6 m x 1.0 m panel to a
   point in the arm's workspace, solves inverse kinematics, lowers the brush
   and makes three side to side passes over the zone, then lifts and homes.
4. `server.js` polls `/api/routine/clean_status` and shows MOVING, CLEANING,
   VERIFYING and IDLE on the dashboard. Once the arm finishes, the zone is
   marked as cleaned.

If the arm controller is not running, the dashboard shows "Arm Link: OFFLINE"
and runs a simulated clean instead, so the dashboard can still be demoed on its
own.

## Setup

You need Node.js 18 or newer, Python 3.10 or newer, and the Arduino IDE with
the ESP32 board package.

### 1. Arm controller

```bash
cd robotic_arm/server
pip install -r requirements.txt
python -m uvicorn app:app --host 0.0.0.0 --port 8000
```

Plug in the Uno before starting. The controller picks the first Arduino port it
finds. With no board connected it falls back to a simulation so the rest still
works. Arm panel: http://localhost:8000

### 2. Dashboard

```bash
cd dashboard
npm install
npm start
```

Open http://localhost:3000. To point it at an arm controller on another
machine, set `ARM_URL`, for example `ARM_URL=http://192.168.1.20:8000 npm start`.

On Windows, `start_all.bat` starts both. On Linux or macOS use `./start_all.sh`.

### 3. ESP32 field node

1. Open `SolarSense/SolarSense.ino` in the Arduino IDE.
2. Install the libraries: BH1750 (Christopher Laws), Adafruit INA219, RTClib,
   OneWire, DallasTemperature, DHT sensor library, Adafruit Unified Sensor.
3. Copy `secrets.h.example` to `secrets.h` and fill in the WiFi name, password
   and the backend URL (`http://<laptop-ip>:3000/api/sensors/data`).
4. Select "ESP32 Dev Module" and upload. Serial monitor at 115200 baud.

PlatformIO also works: `pio run -t upload`.

### 4. Arm firmware

Open `robotic_arm/firmware/robotic_arm/robotic_arm.ino`, select Arduino Uno and
upload, or run `robotic_arm/scripts/upload_firmware.sh`. Wiring is in
`docs/ROBOTIC_ARM.md`.

## Tests

```bash
bash test/run_tests.sh
```
Builds the whole firmware against stub headers and runs the logic tests on the
PC, no ESP32 needed.

```bash
pip install -r tests/requirements.txt
cd dashboard && npm start
pytest -v -s
```
Runs the Selenium dashboard test and the HTTP performance test against the
running dashboard. The same two suites run in Jenkins through the `Jenkinsfile`.

## Main API endpoints

| Service | Method | Path | Purpose |
|---|---|---|---|
| Dashboard | POST | /api/sensors/data | ESP32 posts a reading |
| Dashboard | GET | /api/sensors/latest | Latest reading with AI decision |
| Dashboard | GET | /api/sensors/history | Last 100 readings |
| Dashboard | POST | /api/actuate/clean | Send the arm to a zone |
| Dashboard | GET | /api/arm/status | Is the arm controller reachable |
| Arm | POST | /api/routine/clean_zone | Run a cleaning sweep on a zone |
| Arm | GET | /api/routine/clean_status | Current cleaning state |
| Arm | POST | /api/routine/halt | Stop the arm |
| Arm | GET | /api/status | Joint angles, sonar distance, physics |

Full payload format is in `docs/BACKEND_API.md`.
