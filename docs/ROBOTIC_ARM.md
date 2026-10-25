# Robotic cleaning arm

6-DOF arm driven by an Arduino Uno through a PCA9685 16 channel PWM board, with
an HC-SR04 ultrasonic sensor. The arm controller in `robotic_arm/server` talks
to the Uno over USB serial at 115200 baud.

## Wiring

Arduino Uno to PCA9685

| Uno | PCA9685 |
|---|---|
| A4 (SDA) | SDA |
| A5 (SCL) | SCL |
| 5V | VCC |
| GND | GND |

The servos are powered from the PCA9685 V+ terminal using a separate 5 to 6 V,
3 A or bigger supply. Do not run them from the Uno's 5 V pin.

Servo channels

| Channel | Joint | Notes |
|---|---|---|
| 0 | Base | 90 is straight ahead |
| 1 | Shoulder | 90 is upright, gravity compensated |
| 2 | Elbow | 90 is in line with the upper arm |
| 3 | Wrist pitch | above 90 tilts down |
| 4 | Wrist roll | 90 is neutral |
| 5 | Claw / brush holder | 20 open, 80 closed |

HC-SR04: VCC to 5V, Trig to D8, Echo to D9, GND to GND.

## Serial commands the Uno understands

`PING`, `STATUS`, `ATTACH`, `DETACH`, `STOP`, `HOME`, `SET <ch> <angle>`,
`MOVE <a0> <a1> <a2> <a3> <a4> <a5>`, `PICKDROP`, `REVERSE`, `DIST`,
`I2CSCAN`, `SHOWCALIB`, `SAVECALIB`, `LOADCALIB`, `RESETCALIB`.

## Cleaning a zone

The SolarSense dashboard divides each panel (1.6 m x 1.0 m) into 12 zones and
sends the centre of the dirtiest zone in metres. `zone_to_arm_xy` in
`server/app.py` maps that onto the scaled panel in front of the arm:

- panel rows map to 150 to 260 mm in front of the base
- panel columns map to -110 to +110 mm left and right of centre

The sweep then:

1. attaches the servos and moves above the zone at 90 mm
2. lowers the brush to 45 mm
3. makes three passes, 22 mm either side of the zone centre
4. lifts back to 90 mm (VERIFYING) and returns home

Each move is solved with the inverse kinematics in `physics.py` and played as a
smooth joint trajectory, so the servos do not jump. `POST /api/routine/halt`
stops the sweep between steps.

The panel position constants are at the top of the cleaning block in
`server/app.py` (`PANEL_NEAR_EDGE_MM`, `PANEL_DEPTH_MM`, `PANEL_SPAN_MM`,
`BRUSH_HEIGHT_MM`) and should be measured on your own setup.

## Other arm pages

- `http://localhost:8000` manual control, 3D model, joint torques
- `http://localhost:8000/pick-drop` the 8 step pick and drop routine
- `http://localhost:8000/scanner` QR scanner for phone based dispatch
