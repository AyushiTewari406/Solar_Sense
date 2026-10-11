#!/usr/bin/env bash

echo "=========================================================="
echo " Starting SolarSense Precision AI Field Node Dashboard"
echo " Light Theme Interface & Robotic Arm Zone Actuator"
echo "=========================================================="

CD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$CD_DIR/dashboard"

if [ ! -d "node_modules" ]; then
    echo "Installing dashboard backend dependencies..."
    npm install --silent
fi

echo "Starting SolarSense Dashboard Express server on http://localhost:3000 ..."
npm start
