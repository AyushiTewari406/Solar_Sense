#!/usr/bin/env bash
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../SolarSense"
CXX="${CXX:-g++}"
FLAGS="-std=c++14 -Wall -Wextra -Wno-unused-parameter -I $HERE/shim -I $SRC"

SOURCES="$SRC/src/data/SensorData.cpp $SRC/src/data/DataProcessor.cpp \
$SRC/src/sensors/BH1750Sensor.cpp $SRC/src/sensors/INA219Panel.cpp \
$SRC/src/sensors/RTCManager.cpp $SRC/src/sensors/TemperatureManager.cpp \
$SRC/src/sensors/DHTManager.cpp $SRC/src/sensors/RainGauge.cpp \
$SRC/src/sensors/I2CScanner.cpp $SRC/src/communication/WiFiManager.cpp \
$SRC/src/communication/BackendClient.cpp $SRC/src/logging/SDLogger.cpp \
$SRC/src/ui/Dashboard.cpp $SRC/src/util/Simulation.cpp"

echo "== full firmware build (all modules + sketch) =="
$CXX $FLAGS -x c++ "$SRC/SolarSense.ino" "$HERE/shimdefs.cpp" $SOURCES \
  -o "$HERE/full_build.exe" || { echo "FIRMWARE BUILD FAILED"; exit 1; }
echo "build OK"

echo
echo "== logic tests =="
$CXX $FLAGS "$HERE/main.cpp" "$SRC/src/data/SensorData.cpp" \
  "$SRC/src/data/DataProcessor.cpp" "$SRC/src/logging/SDLogger.cpp" \
  "$SRC/src/communication/BackendClient.cpp" "$SRC/src/ui/Dashboard.cpp" \
  -o "$HERE/logic_tests.exe" || { echo "TEST BUILD FAILED"; exit 1; }

"$HERE/logic_tests.exe"
