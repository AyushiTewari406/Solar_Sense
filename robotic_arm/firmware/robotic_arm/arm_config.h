#ifndef ARM_CONFIG_H
#define ARM_CONFIG_H

#include <Arduino.h>

#define NUM_JOINTS 6

#define LINK_BASE_MM      65.0f
#define LINK_SHOULDER_MM  125.0f
#define LINK_ELBOW_MM     125.0f
#define LINK_WRIST_MM     60.0f
#define LINK_CLAW_MM      45.0f
#define LINK_TOOL_MM      (LINK_WRIST_MM + LINK_CLAW_MM)

struct JointCalibration {
  uint8_t channel;
  const char* name;
  int8_t direction;
  float offsetDeg;
  float minLimitDeg;
  float maxLimitDeg;
  uint16_t minPulseUs;
  uint16_t maxPulseUs;
  uint16_t centerPulseUs;
  bool isContinuous;
  float speedDps;
  bool isCalibrated;
};

#include <avr/pgmspace.h>

const JointCalibration DEFAULT_JOINTS[NUM_JOINTS] PROGMEM = {
  {0, "Base Yaw",        +1, 0.0f,   0.0f, 180.0f,  544, 2400, 1500, false, 180.0f, true},

  {1, "Shoulder Pitch",  +1, 0.0f,  20.0f, 160.0f,  544, 2400, 1500, false, 120.0f, true},

  {2, "Elbow Pitch",     +1, 0.0f,  25.0f, 155.0f,  544, 2400, 1500, false, 180.0f, true},

  {3, "Wrist Pitch",     +1, 0.0f,  20.0f, 160.0f,  544, 2400, 1500, false, 180.0f, true},

  {4, "Wrist Roll",      +1, 0.0f,   0.0f, 180.0f,  544, 2400, 1500, false, 180.0f, true},

  {5, "Gripper Claw",    +1, 0.0f,  15.0f,  85.0f,  544, 2400, 1500, false, 200.0f, true}
};

struct ArmPose {
  const char* name;
  float angles[NUM_JOINTS];
};

const ArmPose POSE_HOME PROGMEM = {
  "HOME",
  {90.0f, 90.0f, 120.0f, 90.0f, 90.0f, 20.0f}
};

const ArmPose POSE_PICK PROGMEM = {
  "PICK",
  {45.0f, 55.0f, 60.0f, 115.0f, 90.0f, 20.0f}
};

const ArmPose POSE_LIFT PROGMEM = {
  "LIFT",
  {45.0f, 85.0f, 95.0f, 90.0f, 90.0f, 80.0f}
};

const ArmPose POSE_DROP PROGMEM = {
  "DROP",
  {135.0f, 60.0f, 65.0f, 110.0f, 90.0f, 80.0f}
};

#endif
