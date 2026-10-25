@echo off
cd /d %~dp0
start "Robotic Arm Controller" cmd /k "cd robotic_arm\server && python -m uvicorn app:app --host 0.0.0.0 --port 8000"
timeout /t 2 >nul
cd dashboard
if not exist node_modules call npm install
npm start
