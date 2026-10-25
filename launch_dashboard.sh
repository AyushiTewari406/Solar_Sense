#!/usr/bin/env bash

CD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$CD_DIR/dashboard"

if [ ! -d "node_modules" ]; then
    echo "Installing dashboard dependencies..."
    npm install --silent
fi

echo "SolarSense dashboard on http://localhost:3000"
echo "Arm controller expected at ${ARM_URL:-http://localhost:8000}"
npm start
