#!/usr/bin/env bash
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$ROOT/robotic_arm/scripts/start_dashboard.sh" &
ARM_PID=$!
trap 'kill $ARM_PID 2>/dev/null || true' EXIT

sleep 2
"$ROOT/launch_dashboard.sh"
