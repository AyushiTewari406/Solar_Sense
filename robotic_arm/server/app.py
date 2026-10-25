import os
import sys
import json
import asyncio
import subprocess
import threading
import time
from pathlib import Path
from typing import List, Optional, Dict, Any

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse, JSONResponse
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

from serial_comm import SerialManager

BASE_DIR = Path(__file__).resolve().parent
PROJECT_DIR = BASE_DIR.parent
FRONTEND_DIR = PROJECT_DIR / "frontend"
FIRMWARE_DIR = PROJECT_DIR / "firmware" / "robotic_arm"

app = FastAPI(title="6-DOF Robotic Arm Real Physics Controller", version="4.0.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

serial_mgr = SerialManager(port=None, baudrate=115200)
connected_clients: List[WebSocket] = []
loop: Optional[asyncio.AbstractEventLoop] = None

routine_lock = threading.Lock()
routine_running = False

INVENTORY_DATABASE: Dict[str, Dict[str, Any]] = {
    "INV-BOX-01": {
        "id": "INV-BOX-01",
        "name": "Box #1 (Electronics Module)",
        "sku": "SKU-ELEC-01",
        "category": "Components",
        "pick_base": 45.0,
        "drop_base": 135.0,
        "is_reverse": False,
        "location": "Station A (45°)",
        "target_bin": "Drop Bin 1 (135°)",
        "status": "Ready for Pickup",
        "scans_count": 0,
        "last_scanned": None
    },
    "INV-BOX-02": {
        "id": "INV-BOX-02",
        "name": "Box #2 (Mechanical Tools)",
        "sku": "SKU-TOOL-02",
        "category": "Tools",
        "pick_base": 45.0,
        "drop_base": 160.0,
        "is_reverse": False,
        "location": "Station A (45°)",
        "target_bin": "Drop Bin 2 (160°)",
        "status": "Ready for Pickup",
        "scans_count": 0,
        "last_scanned": None
    },
    "INV-BOX-03": {
        "id": "INV-BOX-03",
        "name": "Box #3 (Sensors & Actuators)",
        "sku": "SKU-SENS-03",
        "category": "Electronics",
        "pick_base": 60.0,
        "drop_base": 135.0,
        "is_reverse": False,
        "location": "Station B (60°)",
        "target_bin": "Drop Bin 1 (135°)",
        "status": "Ready for Pickup",
        "scans_count": 0,
        "last_scanned": None
    },
    "RETURN-BIN-01": {
        "id": "RETURN-BIN-01",
        "name": "Return Bin 1 to Station A (Reverse)",
        "sku": "SKU-RET-01",
        "category": "Restock",
        "pick_base": 135.0,
        "drop_base": 45.0,
        "is_reverse": True,
        "location": "Drop Bin 1 (135°)",
        "target_bin": "Station A (45°)",
        "status": "In Drop Bin",
        "scans_count": 0,
        "last_scanned": None
    }
}

class ServoCommand(BaseModel):
    channel: int
    angle: float

class MoveAllCommand(BaseModel):
    angles: List[float]
    speed: Optional[float] = None

class SpeedCommand(BaseModel):
    speed: float

class RangeCommand(BaseModel):
    channel: int
    maxAngle: float

class PortConnectCommand(BaseModel):
    port: str
    baudrate: Optional[int] = 115200

class RoutineCommand(BaseModel):
    reverse: Optional[bool] = False
    pick_base: Optional[float] = 45.0
    drop_base: Optional[float] = 135.0

class PhysicsConfigCommand(BaseModel):
    payload_mass_kg: Optional[float] = None
    compensation_enabled: Optional[bool] = None
    compensation_gain: Optional[float] = None

class QrScanRequest(BaseModel):
    qr_data: str
    auto_execute: Optional[bool] = True
    source: Optional[str] = "mobile_scanner"

class DispatchExecuteRequest(BaseModel):
    item_id: Optional[str] = None
    pick_base: Optional[float] = 45.0
    drop_base: Optional[float] = 135.0
    reverse: Optional[bool] = False
    name: Optional[str] = "Direct Dispatch"

class IkSolveRequest(BaseModel):
    x: float
    y: float
    z: float

class IkMoveRequest(BaseModel):
    x: float
    y: float
    z: float
    duration: Optional[float] = 1.5
    num_steps: Optional[int] = 20

async def broadcast_ws(message: dict):
    if not connected_clients:
        return
    text = json.dumps(message)
    dead_clients = []
    for ws in connected_clients:
        try:
            await ws.send_text(text)
        except Exception:
            dead_clients.append(ws)
    for ws in dead_clients:
        if ws in connected_clients:
            connected_clients.remove(ws)

def on_serial_status(status_data: Dict[str, Any]):
    if loop and loop.is_running():
        asyncio.run_coroutine_threadsafe(
            broadcast_ws({"type": "telemetry", "data": status_data}),
            loop
        )

def on_serial_log(direction: str, text: str):
    if loop and loop.is_running():
        asyncio.run_coroutine_threadsafe(
            broadcast_ws({"type": "log", "direction": direction, "text": text, "timestamp": time.time()}),
            loop
        )

serial_mgr.add_status_listener(on_serial_status)
serial_mgr.add_log_listener(on_serial_log)

@app.on_event("startup")
async def startup_event():
    global loop
    loop = asyncio.get_running_loop()

app.mount("/static", StaticFiles(directory=str(FRONTEND_DIR)), name="static")

@app.api_route("/", methods=["GET", "HEAD"])
async def get_index():
    return FileResponse(FRONTEND_DIR / "index.html")

@app.api_route("/pick-drop", methods=["GET", "HEAD"])
@app.api_route("/pick_drop.html", methods=["GET", "HEAD"])
async def get_pick_drop_page():
    return FileResponse(FRONTEND_DIR / "pick_drop.html")

@app.api_route("/scanner", methods=["GET", "HEAD"])
@app.api_route("/scanner.html", methods=["GET", "HEAD"])
async def get_scanner_page():
    return FileResponse(FRONTEND_DIR / "scanner.html")

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    connected_clients.append(websocket)
    try:
        await websocket.send_text(json.dumps({
            "type": "telemetry",
            "data": serial_mgr.status
        }))
        while True:
            data = await websocket.receive_text()
            try:
                msg = json.loads(data)
                action = msg.get("action")
                if action == "ping":
                    await websocket.send_text(json.dumps({"type": "pong"}))
                elif action == "set_servo":
                    ch = msg.get("channel")
                    ang = msg.get("angle")
                    if ch is not None and ang is not None:
                        serial_mgr.send_raw(f"SET {ch} {float(ang):.2f}")
                elif action == "hold":
                    ch = msg.get("channel")
                    if ch is not None:
                        serial_mgr.send_raw(f"HOLD {ch}")
                elif action == "range":
                    ch = msg.get("channel")
                    max_a = msg.get("maxAngle")
                    if ch is not None and max_a is not None:
                        serial_mgr.send_raw(f"RANGE {ch} {float(max_a):.1f}")
                elif action == "raw":
                    cmd = msg.get("cmd")
                    if cmd:
                        serial_mgr.send_raw(cmd)
            except json.JSONDecodeError:
                pass
    except WebSocketDisconnect:
        if websocket in connected_clients:
            connected_clients.remove(websocket)
    except Exception:
        if websocket in connected_clients:
            connected_clients.remove(websocket)

@app.get("/api/status")
async def get_status():
    return serial_mgr.status

@app.get("/api/ports")
async def get_ports():
    return {
        "ports": serial_mgr.list_available_ports(),
        "current": serial_mgr.port,
        "connected": serial_mgr.is_connected,
        "simulated": serial_mgr.is_simulated
    }

@app.post("/api/connect")
async def connect_port(cmd: PortConnectCommand):
    serial_mgr.is_flashing = False
    serial_mgr.auto_reconnect = True
    success = serial_mgr.connect(cmd.port, cmd.baudrate or 115200)
    return {"success": success, "port": cmd.port, "connected": serial_mgr.is_connected}

@app.post("/api/disconnect")
async def disconnect_port():
    serial_mgr.disconnect(for_flash=True)
    return {"success": True, "connected": False}

@app.post("/api/simulation")
async def toggle_simulation(enable: bool = True):
    if enable:
        serial_mgr.enable_simulation_mode()
    else:
        serial_mgr.is_simulated = False
        serial_mgr.status["simulated"] = False
    return {"simulated": serial_mgr.is_simulated}

@app.post("/api/set_servo")
async def set_servo(cmd: ServoCommand):
    if not (0 <= cmd.channel < 6):
        raise HTTPException(status_code=400, detail="Channel must be 0 to 5")
    angle = max(0.0, min(180.0, float(cmd.angle)))
    success = serial_mgr.send_raw(f"SET {cmd.channel} {angle:.2f}")
    return {"success": success, "channel": cmd.channel, "angle": angle}

@app.post("/api/move_all")
async def move_all(cmd: MoveAllCommand):
    if len(cmd.angles) != 6:
        raise HTTPException(status_code=400, detail="Must provide exactly 6 angles (CH 0-5)")
    cmd_str = f"MOVE {' '.join(f'{a:.2f}' for a in cmd.angles)}"
    if cmd.speed is not None:
        cmd_str += f" {cmd.speed:.1f}"
    success = serial_mgr.send_raw(cmd_str)
    return {"success": success, "angles": cmd.angles}

@app.post("/api/speed")
async def set_speed(cmd: SpeedCommand):
    spd = max(5.0, min(360.0, float(cmd.speed)))
    success = serial_mgr.send_raw(f"SPEED {spd:.1f}")
    return {"success": success, "speed": spd}

@app.post("/api/attach")
async def attach_all():
    success = serial_mgr.send_raw("ATTACH")
    return {"success": success, "attached": True}

@app.post("/api/detach")
async def detach_all():
    success = serial_mgr.send_raw("DETACH")
    return {"success": success, "attached": False}

@app.post("/api/isolate/{channel}")
async def isolate_channel(channel: int):
    if not (0 <= channel < 6):
        raise HTTPException(status_code=400, detail="Channel must be 0 to 5")
    success = serial_mgr.send_raw(f"ISOLATE {channel}")
    return {"success": success, "channel": channel}

@app.post("/api/relax/{channel}")
async def relax_channel(channel: int):
    if not (0 <= channel < 6):
        raise HTTPException(status_code=400, detail="Channel must be 0 to 5")
    success = serial_mgr.send_raw(f"RELAX {channel}")
    return {"success": success, "channel": channel}

@app.post("/api/hold/{channel}")
async def hold_channel(channel: int):
    if not (0 <= channel < 6):
        raise HTTPException(status_code=400, detail="Channel must be 0 to 5")
    success = serial_mgr.send_raw(f"HOLD {channel}")
    return {"success": success, "channel": channel}

@app.post("/api/range")
async def range_channel(cmd: RangeCommand):
    if not (0 <= cmd.channel < 6):
        raise HTTPException(status_code=400, detail="Channel must be 0 to 5")
    max_a = max(45.0, min(360.0, float(cmd.maxAngle)))
    success = serial_mgr.send_raw(f"RANGE {cmd.channel} {max_a:.1f}")
    return {"success": success, "channel": cmd.channel, "maxAngle": max_a}

@app.post("/api/pose/home_safe")
async def home_safe_endpoint():
    cur = serial_mgr.status.get("cur", [90.0, 90.0, 90.0, 90.0, 90.0, 20.0])
    plan = serial_mgr.physics.plan_safe_homing_path(cur)

    def run_safe_homing_bg():
        serial_mgr._notify_log("SYS", "🏠 Initiating Collision-Free Return to Safe Home (0, 0, 420mm)...")
        for idx, wp in enumerate(plan["waypoints"]):
            serial_mgr._notify_log("SYS", f"[{wp['phase'].upper()} ({idx+1}/{len(plan['waypoints'])})] {wp['description']}")
            target = wp["target_angles"]
            dur = wp["duration"]
            cur_phase = serial_mgr.status.get("cur", cur)
            traj = serial_mgr.physics.generate_joint_trajectory(cur_phase, target, duration_sec=dur, num_steps=20)
            dt = dur / float(len(traj))
            for pt in traj:
                ang = pt["angles"]
                serial_mgr.send_raw(f"MOVE {' '.join(f'{a:.2f}' for a in ang)}")
                time.sleep(dt)
            serial_mgr.send_raw(f"MOVE {' '.join(f'{a:.2f}' for a in target)}")
            time.sleep(0.2)
        serial_mgr._notify_log("SYS", "✅ Successfully Returned to Safe Home: (0.00, 0.00, 420.00) mm")

    threading.Thread(target=run_safe_homing_bg, daemon=True).start()

    return {
        "success": True,
        "action": "home_safe",
        "home_coords": plan["home_coords"],
        "phases": len(plan["waypoints"]),
        "plan": plan
    }

@app.get("/api/trajectory/home_plan")
async def get_home_plan_endpoint():
    cur = serial_mgr.status.get("cur", [90.0, 90.0, 90.0, 90.0, 90.0, 20.0])
    return serial_mgr.physics.plan_safe_homing_path(cur)

@app.post("/api/pose/{name}")
async def set_pose(name: str):
    name_clean = name.upper().strip()
    if name_clean in ["HOME", "STANDBY", "HOME_SAFE"]:
        return await home_safe_endpoint()

    valid_poses = {
        "PICK": "PICK",
        "LIFT": "LIFT",
        "DROP": "DROP",
        "CLAW": "CLAW"
    }
    if name_clean not in valid_poses:
        raise HTTPException(status_code=400, detail=f"Unknown pose: {name}")
    success = serial_mgr.send_raw(valid_poses[name_clean])
    return {"success": success, "pose": name_clean}

@app.post("/api/ik/solve")
async def solve_ik_endpoint(req: IkSolveRequest):
    cur = serial_mgr.status.get("cur", [90.0, 90.0, 90.0, 90.0, 90.0, 20.0])
    result = serial_mgr.physics.inverse_kinematics_3d(req.x, req.y, req.z, cur_angles=cur)
    return result

@app.post("/api/ik/move")
async def move_to_xyz_endpoint(req: IkMoveRequest):
    cur = serial_mgr.status.get("cur", [90.0, 90.0, 90.0, 90.0, 90.0, 20.0])
    ik_sol = serial_mgr.physics.inverse_kinematics_3d(req.x, req.y, req.z, cur_angles=cur)

    if not ik_sol["reachable"] and ik_sol["position_error_mm"] > 40.0:
        raise HTTPException(
            status_code=400,
            detail=f"Target Cartesian coordinate ({req.x}, {req.y}, {req.z}) is unreachable (reach error: {ik_sol['position_error_mm']}mm)"
        )

    target_angles = ik_sol["solved_angles"]
    duration = max(0.5, min(10.0, float(req.duration or 1.5)))
    steps = max(10, min(50, int(req.num_steps or 25)))
    traj = serial_mgr.physics.generate_joint_trajectory(cur, target_angles, duration_sec=duration, num_steps=steps)

    def run_traj_background():
        dt = duration / float(len(traj))
        for pt in traj:
            ang = pt["angles"]
            serial_mgr.send_raw(f"MOVE {' '.join(f'{a:.2f}' for a in ang)}")
            time.sleep(dt)
        serial_mgr.send_raw(f"MOVE {' '.join(f'{a:.2f}' for a in target_angles)}")

    threading.Thread(target=run_traj_background, daemon=True).start()

    return {
        "success": True,
        "target": [round(req.x, 2), round(req.y, 2), round(req.z, 2)],
        "solved_angles": target_angles,
        "achieved_coords": ik_sol["achieved_coords"],
        "position_error_mm": ik_sol["position_error_mm"],
        "duration_sec": duration,
        "trajectory_steps": len(traj)
    }

@app.post("/api/routine/pick_drop")
async def run_pick_drop_routine(cmd: RoutineCommand):
    rev_str = "REVERSE" if cmd.reverse else "PICKDROP"
    success = serial_mgr.send_raw(rev_str)
    return {
        "success": success,
        "routine": "pick_drop",
        "reverse": cmd.reverse,
        "pick_base": cmd.pick_base,
        "drop_base": cmd.drop_base
    }

@app.post("/api/routine/halt")
async def halt_routine():
    serial_mgr.send_raw("STOP")
    with clean_lock:
        clean_job["abort"] = True
    return {"success": True, "halted": True}

PANEL_WIDTH_M = 1.6
PANEL_HEIGHT_M = 1.0
PANEL_NEAR_EDGE_MM = 150.0
PANEL_DEPTH_MM = 110.0
PANEL_SPAN_MM = 220.0
BRUSH_HEIGHT_MM = 45.0
BRUSH_LIFT_MM = 90.0
SWEEP_HALF_WIDTH_MM = 22.0
SWEEP_PASSES = 3

clean_lock = threading.Lock()
clean_job: Dict[str, Any] = {
    "state": "IDLE",
    "panel": None,
    "zone_id": None,
    "started_at": None,
    "finished_at": None,
    "passes_done": 0,
    "error": None,
    "abort": False
}

class CleanZoneRequest(BaseModel):
    panel: Optional[str] = "panelB"
    zone_id: Optional[str] = "Z03"
    x: float = 0.8
    y: float = 0.5
    passes: Optional[int] = SWEEP_PASSES

def zone_to_arm_xy(x_m: float, y_m: float):
    x_m = max(0.0, min(PANEL_WIDTH_M, float(x_m)))
    y_m = max(0.0, min(PANEL_HEIGHT_M, float(y_m)))
    arm_x = PANEL_NEAR_EDGE_MM + (y_m / PANEL_HEIGHT_M) * PANEL_DEPTH_MM
    arm_y = (x_m / PANEL_WIDTH_M - 0.5) * PANEL_SPAN_MM
    return round(arm_x, 2), round(arm_y, 2)

def _set_clean_state(state: str, **extra):
    with clean_lock:
        clean_job["state"] = state
        clean_job.update(extra)

def _glide_to(x_mm: float, y_mm: float, z_mm: float, duration: float = 0.8, steps: int = 16):
    cur = serial_mgr.status.get("cur", [90.0, 90.0, 90.0, 90.0, 90.0, 20.0])
    sol = serial_mgr.physics.inverse_kinematics_3d(x_mm, y_mm, z_mm, cur_angles=cur)
    if not sol["reachable"] and sol["position_error_mm"] > 40.0:
        raise RuntimeError(f"zone point ({x_mm}, {y_mm}, {z_mm}) is out of reach")
    target = list(sol["solved_angles"])
    target[5] = 80.0
    traj = serial_mgr.physics.generate_joint_trajectory(cur, target, duration_sec=duration, num_steps=steps)
    dt = duration / float(max(1, len(traj)))
    for pt in traj:
        if clean_job["abort"]:
            raise RuntimeError("cleaning halted by operator")
        ang = list(pt["angles"])
        ang[5] = 80.0
        serial_mgr.send_raw(f"MOVE {' '.join(f'{a:.2f}' for a in ang)}")
        time.sleep(dt)
    serial_mgr.send_raw(f"MOVE {' '.join(f'{a:.2f}' for a in target)}")

def run_clean_zone(arm_x: float, arm_y: float, passes: int):
    try:
        serial_mgr.send_raw("ATTACH")
        _set_clean_state("MOVING")
        _glide_to(arm_x, arm_y, BRUSH_LIFT_MM, duration=1.4, steps=24)
        _glide_to(arm_x, arm_y, BRUSH_HEIGHT_MM, duration=0.6, steps=10)
        _set_clean_state("CLEANING")
        for i in range(passes):
            _glide_to(arm_x, arm_y - SWEEP_HALF_WIDTH_MM, BRUSH_HEIGHT_MM, duration=0.7, steps=12)
            _glide_to(arm_x, arm_y + SWEEP_HALF_WIDTH_MM, BRUSH_HEIGHT_MM, duration=0.7, steps=12)
            with clean_lock:
                clean_job["passes_done"] = i + 1
        _set_clean_state("VERIFYING")
        _glide_to(arm_x, arm_y, BRUSH_LIFT_MM, duration=0.6, steps=10)
        serial_mgr.send_raw("HOME")
        time.sleep(1.0)
        _set_clean_state("IDLE", finished_at=time.time())
    except Exception as exc:
        serial_mgr.send_raw("HOME")
        _set_clean_state("IDLE", finished_at=time.time(), error=str(exc))

@app.post("/api/routine/clean_zone")
async def clean_zone_endpoint(req: CleanZoneRequest):
    with clean_lock:
        if clean_job["state"] != "IDLE":
            raise HTTPException(status_code=409, detail=f"Arm is busy ({clean_job['state']})")
        arm_x, arm_y = zone_to_arm_xy(req.x, req.y)
        passes = max(1, min(6, int(req.passes or SWEEP_PASSES)))
        clean_job.update({
            "state": "MOVING",
            "panel": req.panel,
            "zone_id": req.zone_id,
            "started_at": time.time(),
            "finished_at": None,
            "passes_done": 0,
            "error": None,
            "abort": False,
            "arm_target_mm": [arm_x, arm_y, BRUSH_HEIGHT_MM]
        })
    threading.Thread(target=run_clean_zone, args=(arm_x, arm_y, passes), daemon=True).start()
    await broadcast_ws({"type": "clean_zone", "job": dict(clean_job), "timestamp": time.time()})
    return {"success": True, "job": dict(clean_job)}

@app.get("/api/routine/clean_status")
async def clean_status_endpoint():
    with clean_lock:
        job = dict(clean_job)
    job["connected"] = serial_mgr.is_connected
    job["simulated"] = serial_mgr.is_simulated
    return job

@app.post("/api/physics/config")
async def configure_physics(cmd: PhysicsConfigCommand):
    if cmd.payload_mass_kg is not None:
        serial_mgr.physics.set_payload_mass(cmd.payload_mass_kg)
    if cmd.compensation_enabled is not None:
        serial_mgr.physics.set_compensation_enabled(cmd.compensation_enabled)
    if cmd.compensation_gain is not None:
        serial_mgr.physics.set_compensation_gain(cmd.compensation_gain)
    serial_mgr._update_physics_state()
    serial_mgr._notify_listeners(serial_mgr.status)
    return {
        "success": True,
        "physics": serial_mgr.status.get("physics")
    }

@app.get("/api/sonar")
async def read_sonar():
    serial_mgr.send_raw("DIST")
    return {"distance_cm": serial_mgr.status.get("dist", -1.0)}

@app.post("/api/flash")
async def flash_firmware():
    port = serial_mgr.port or "/dev/ttyUSB0"
    serial_mgr.disconnect(for_flash=True)
    time.sleep(1.2)

    arduino_cli = shutil_which("arduino-cli") or "/usr/bin/arduino-cli" or str(Path.home() / ".local/bin/arduino-cli")
    fqbn = "arduino:avr:uno"

    try:
        comp = subprocess.run(
            [arduino_cli, "compile", "--fqbn", fqbn, str(FIRMWARE_DIR)],
            capture_output=True, text=True, timeout=30
        )
        if comp.returncode != 0:
            serial_mgr.resume_after_flash(port)
            return JSONResponse(status_code=500, content={"success": False, "stage": "compile", "error": comp.stderr})

        up = subprocess.run(
            [arduino_cli, "upload", "-p", port, "--fqbn", fqbn, str(FIRMWARE_DIR)],
            capture_output=True, text=True, timeout=30
        )
        time.sleep(1.5)
        serial_mgr.resume_after_flash(port)

        if up.returncode == 0:
            return {"success": True, "message": "Firmware compiled and uploaded successfully!"}
        else:
            return JSONResponse(status_code=500, content={"success": False, "stage": "upload", "error": up.stderr})
    except Exception as e:
        serial_mgr.resume_after_flash(port)
        return JSONResponse(status_code=500, content={"success": False, "error": str(e)})

def shutil_which(cmd: str) -> Optional[str]:
    import shutil
    return shutil.which(cmd)

DISPATCH_HISTORY: List[Dict[str, Any]] = []

def get_primary_lan_ip() -> str:
    ips = []
    try:
        output = subprocess.check_output(['hostname', '-I'], text=True).strip().split()
        for ip in output:
            if ':' not in ip and not ip.startswith('127.'):
                ips.append(ip)
    except Exception:
        pass

    primary = "127.0.0.1"
    for ip in ips:
        if not ip.startswith("172.17.") and not ip.startswith("172.18."):
            primary = ip
            break
    if primary == "127.0.0.1" and ips:
        primary = ips[0]
    return primary

def parse_qr_payload(code: str) -> Dict[str, Any]:
    code = code.strip()
    if code in INVENTORY_DATABASE:
        return INVENTORY_DATABASE[code]

    try:
        obj = json.loads(code)
        if isinstance(obj, dict):
            p_base = float(obj.get("pick_base", obj.get("pick", 45.0)))
            d_base = float(obj.get("drop_base", obj.get("drop", 135.0)))
            name = str(obj.get("name", f"Puck {obj.get('id', 'Custom')}"))
            item = {
                "id": str(obj.get("id", f"QR-{int(time.time())}")),
                "name": name,
                "sku": str(obj.get("sku", f"SKU-CUSTOM")),
                "category": str(obj.get("category", "Custom QR")),
                "pick_base": p_base,
                "drop_base": d_base,
                "is_reverse": bool(obj.get("reverse", False)),
                "location": str(obj.get("location", f"Pick Station ({p_base:.1f}°)")),
                "target_bin": str(obj.get("target_bin", f"Drop Bin ({d_base:.1f}°)")),
                "status": "Ready for Pickup",
                "scans_count": 0,
                "last_scanned": None
            }
            INVENTORY_DATABASE[item["id"]] = item
            return item
    except Exception:
        pass

    if "," in code or ";" in code:
        parts = code.replace(";", ",").split(",")
        p_base = None
        d_base = None
        for p in parts:
            p = p.strip()
            if ":" in p or "=" in p:
                k, v = p.replace("=", ":").split(":", 1)
                k = k.strip().upper()
                try:
                    if "PICK" in k: p_base = float(v.strip())
                    elif "DROP" in k: d_base = float(v.strip())
                except ValueError:
                    pass
            else:
                try:
                    val = float(p)
                    if p_base is None: p_base = val
                    elif d_base is None: d_base = val
                except ValueError:
                    pass
        if p_base is not None and d_base is not None:
            item = {
                "id": f"QR-{int(p_base)}-{int(d_base)}",
                "name": f"Coordinate Target ({p_base:.0f}° ➔ {d_base:.0f}°)",
                "sku": f"SKU-{int(p_base)}{int(d_base)}",
                "category": "Direct Angles",
                "pick_base": p_base,
                "drop_base": d_base,
                "is_reverse": False,
                "location": f"Pick Station ({p_base:.1f}°)",
                "target_bin": f"Drop Bin ({d_base:.1f}°)",
                "status": "Ready for Pickup",
                "scans_count": 0,
                "last_scanned": None
            }
            INVENTORY_DATABASE[item["id"]] = item
            return item

    item = {
        "id": code,
        "name": f"Scanned Item ({code})",
        "sku": f"SKU-{code[:6]}",
        "category": "Custom Scanned",
        "pick_base": 45.0,
        "drop_base": 135.0,
        "is_reverse": False,
        "location": "Station A (45°)",
        "target_bin": "Drop Bin 1 (135°)",
        "status": "Ready for Pickup",
        "scans_count": 0,
        "last_scanned": None
    }
    INVENTORY_DATABASE[code] = item
    return item

@app.get("/api/inventory")
async def get_inventory():
    return {"items": list(INVENTORY_DATABASE.values())}

@app.get("/api/network_info")
async def get_network_info_endpoint():
    lan_ip = get_primary_lan_ip()
    return {
        "primary_ip": lan_ip,
        "port": 8000,
        "scanner_url": f"http://{lan_ip}:8000/scanner",
        "pick_drop_url": f"http://{lan_ip}:8000/pick-drop",
        "all_ips": subprocess.check_output(['hostname', '-I'], text=True).strip().split() if shutil_which('hostname') else [lan_ip]
    }

@app.get("/api/dispatch/history")
async def get_dispatch_history():
    return {"history": DISPATCH_HISTORY}

@app.post("/api/dispatch/clear")
async def clear_dispatch_history():
    DISPATCH_HISTORY.clear()
    return {"success": True}

@app.post("/api/dispatch/execute")
async def execute_dispatch(req: DispatchExecuteRequest):
    item = None
    if req.item_id and req.item_id in INVENTORY_DATABASE:
        item = INVENTORY_DATABASE[req.item_id]

    pick_b = float(req.pick_base if req.pick_base is not None else (item["pick_base"] if item else 45.0))
    drop_b = float(req.drop_base if req.drop_base is not None else (item["drop_base"] if item else 135.0))
    is_rev = bool(req.reverse if req.reverse is not None else (item.get("is_reverse", False) if item else False))

    cmd_str = f"REVERSE {pick_b:.1f} {drop_b:.1f}" if is_rev else f"PICKDROP {pick_b:.1f} {drop_b:.1f}"
    serial_mgr.send_raw(cmd_str)

    history_record = {
        "id": f"DSP-{int(time.time()*1000)}",
        "item_id": req.item_id or "MANUAL",
        "item_name": (item["name"] if item else req.name) or "Manual Dispatch",
        "sku": (item.get("sku", "MANUAL") if item else "MANUAL"),
        "pick_base": pick_b,
        "drop_base": drop_b,
        "source": "laptop_dashboard",
        "timestamp": time.strftime("%H:%M:%S"),
        "status": "In Progress",
        "auto_executed": True
    }
    DISPATCH_HISTORY.insert(0, history_record)
    if len(DISPATCH_HISTORY) > 50:
        DISPATCH_HISTORY.pop()

    await broadcast_ws({
        "type": "qr_dispatch",
        "item": item or {
            "id": req.item_id or "MANUAL",
            "name": req.name or "Manual Dispatch",
            "pick_base": pick_b,
            "drop_base": drop_b,
            "is_reverse": is_rev
        },
        "history": history_record,
        "auto_executed": True,
        "source": "laptop_dashboard",
        "timestamp": time.time()
    })

    return {"success": True, "command": cmd_str, "record": history_record}

@app.post("/api/scan_qr")
async def process_qr_scan(req: QrScanRequest):
    item = parse_qr_payload(req.qr_data)
    item["scans_count"] = item.get("scans_count", 0) + 1
    item["last_scanned"] = time.strftime("%Y-%m-%d %H:%M:%S")

    is_auto = bool(req.auto_execute)
    is_rev = item.get("is_reverse", False)
    pick_b = float(item.get("pick_base", 45.0))
    drop_b = float(item.get("drop_base", 135.0))

    if is_auto:
        cmd_str = f"REVERSE {pick_b:.1f} {drop_b:.1f}" if is_rev else f"PICKDROP {pick_b:.1f} {drop_b:.1f}"
        serial_mgr.send_raw(cmd_str)
        item["status"] = "Processing Pick & Drop"
    else:
        item["status"] = "Queued on Dashboard"

    history_record = {
        "id": f"DSP-{int(time.time()*1000)}",
        "item_id": item["id"],
        "item_name": item["name"],
        "sku": item.get("sku", ""),
        "pick_base": pick_b,
        "drop_base": drop_b,
        "source": req.source or "mobile_scanner",
        "timestamp": time.strftime("%H:%M:%S"),
        "status": "In Progress" if is_auto else "Queued",
        "auto_executed": is_auto
    }
    DISPATCH_HISTORY.insert(0, history_record)
    if len(DISPATCH_HISTORY) > 50:
        DISPATCH_HISTORY.pop()

    await broadcast_ws({
        "type": "qr_dispatch",
        "item": item,
        "history": history_record,
        "auto_executed": is_auto,
        "source": req.source or "mobile_scanner",
        "timestamp": time.time()
    })

    return {
        "success": True,
        "item": item,
        "routine_triggered": is_auto,
        "history": history_record,
        "is_reverse": is_rev
    }
