import math
from typing import Dict, List, Tuple, Any, Optional

class RoboticArmPhysics:
    def __init__(self):
        self.g = 9.80665

        self.L_base = 0.065
        self.L_shoulder = 0.125
        self.L_elbow = 0.125
        self.L_wrist = 0.060
        self.L_claw = 0.045
        self.L_tool = self.L_wrist + self.L_claw

        self.m_base = 0.220
        self.m_shoulder = 0.160
        self.m_elbow = 0.130
        self.m_wrist = 0.080
        self.m_gripper = 0.065
        self.payload_mass = 0.040

        self.r_com_shoulder = 0.45
        self.r_com_elbow = 0.45
        self.r_com_wrist = 0.50

        self.I_shoulder = self.m_shoulder * (self.L_shoulder ** 2) / 3.0
        self.I_elbow = self.m_elbow * (self.L_elbow ** 2) / 3.0
        self.I_wrist = (self.m_wrist + self.m_gripper + self.payload_mass) * (self.L_tool ** 2) / 3.0

        self.joint_friction = [0.08, 0.12, 0.10, 0.05, 0.03, 0.02]

        self.k_stiffness = [0.045, 0.022, 0.024, 0.035, 0.050, 0.050]

        self.max_torque = [1.2, 1.2, 1.2, 0.4, 0.4, 0.4]

        self.compensation_gain = 1.0
        self.compensation_enabled = True

        self.HOME_ANGLES = [90.00, 90.00, 120.00, 90.00, 90.00, 20.00]
        self.HOME_COORDS = [0.00, 0.00, round((self.L_base + self.L_shoulder + self.L_elbow + self.L_tool) * 1000.0, 2)]

    def set_payload_mass(self, mass_kg: float):
        self.payload_mass = max(0.0, min(0.500, float(mass_kg)))
        self.I_wrist = (self.m_wrist + self.m_gripper + self.payload_mass) * (self.L_tool ** 2) / 3.0

    def set_compensation_enabled(self, enabled: bool):
        self.compensation_enabled = bool(enabled)

    def set_compensation_gain(self, gain: float):
        self.compensation_gain = max(0.0, min(2.0, float(gain)))

    def calculate_forward_kinematics(self, angles_deg: List[float]) -> Dict[str, Any]:
        if len(angles_deg) < 6:
            angles_deg = list(angles_deg) + [90.0] * (6 - len(angles_deg))

        q_base = math.radians(angles_deg[0] - 90.0)

        th1 = math.radians(90.0 - angles_deg[1])
        th2 = th1 + math.radians(90.0 - angles_deg[2])
        th3 = th2 + math.radians(angles_deg[3] - 90.0)

        r0 = 0.0
        z0 = self.L_base

        r1 = self.L_shoulder * math.sin(th1)
        z1 = z0 + self.L_shoulder * math.cos(th1)

        r2 = r1 + self.L_elbow * math.sin(th2)
        z2 = z1 + self.L_elbow * math.cos(th2)

        r3 = r2 + self.L_tool * math.sin(th3)
        z3 = z2 + self.L_tool * math.cos(th3)

        def to_3d(r_m, z_m):
            x_mm = r_m * math.cos(q_base) * 1000.0
            y_mm = r_m * math.sin(q_base) * 1000.0
            z_mm = z_m * 1000.0
            return [round(x_mm, 2), round(y_mm, 2), round(z_mm, 2)]

        end_effector = to_3d(r3, z3)
        reach_mm = round(math.sqrt(r3**2 + (z3 - self.L_base)**2) * 1000.0, 2)
        total_dist_mm = round(math.sqrt(r3**2 + z3**2) * 1000.0, 2)

        return {
            "joint_positions": {
                "origin": [0.0, 0.0, 0.0],
                "base": [0.0, 0.0, round(self.L_base * 1000.0, 2)],
                "shoulder": to_3d(0.0, self.L_base),
                "elbow": to_3d(r1, z1),
                "wrist": to_3d(r2, z2),
                "end_effector": end_effector
            },
            "end_effector": end_effector,
            "reach_mm": reach_mm,
            "total_dist_mm": total_dist_mm,
            "planar_angles_rad": [th1, th2, th3]
        }

    def inverse_kinematics_3d(self, target_x_mm: float, target_y_mm: float, target_z_mm: float,
                              cur_angles: Optional[List[float]] = None) -> Dict[str, Any]:
        tx = float(target_x_mm)
        ty = float(target_y_mm)
        tz = float(target_z_mm)

        if math.hypot(tx, ty, tz - self.HOME_COORDS[2]) < 4.0:
            return {
                "reachable": True,
                "target": [round(tx, 2), round(ty, 2), round(tz, 2)],
                "solved_angles": list(self.HOME_ANGLES),
                "achieved_coords": [0.0, 0.0, self.HOME_COORDS[2]],
                "position_error_mm": 0.0
            }

        r_target = math.hypot(tx, ty)
        if r_target < 1e-4:
            base_deg = 90.0
        else:
            base_rad = math.atan2(ty, tx)
            base_deg = max(0.0, min(180.0, 90.0 + math.degrees(base_rad)))

        max_reach_mm = (self.L_shoulder + self.L_elbow + self.L_tool) * 1000.0
        min_reach_mm = 80.0
        z_shoulder_mm = self.L_base * 1000.0
        dist_from_shoulder = math.hypot(r_target, tz - z_shoulder_mm)
        reachable = True

        r_work = r_target
        z_work = tz

        if dist_from_shoulder > max_reach_mm:
            reachable = False
            scale = (max_reach_mm * 0.998) / dist_from_shoulder
            r_work = r_target * scale
            z_work = z_shoulder_mm + (tz - z_shoulder_mm) * scale
        elif dist_from_shoulder < min_reach_mm:
            reachable = False
            scale = (min_reach_mm * 1.02) / max(1.0, dist_from_shoulder)
            r_work = r_target * scale
            z_work = z_shoulder_mm + (tz - z_shoulder_mm) * scale

        seeds = [
            [90.0, 90.0, 90.0],
            [60.0, 50.0, 105.0],
            [75.0, 80.0, 90.0],
            [45.0, 45.0, 110.0],
            [30.0, 50.0, 120.0],
            [95.0, 95.0, 85.0]
        ]
        if cur_angles and len(cur_angles) >= 4:
            seeds.insert(0, [cur_angles[1], cur_angles[2], cur_angles[3]])

        best_q = None
        best_err = float('inf')

        deg2rad = math.pi / 180.0

        for seed in seeds:
            q = list(seed)
            for it in range(35):
                th1 = math.radians(90.0 - q[0])
                th2 = th1 + math.radians(90.0 - q[1])
                th3 = th2 + math.radians(q[2] - 90.0)

                r_calc = (self.L_shoulder * math.sin(th1) + self.L_elbow * math.sin(th2) + self.L_tool * math.sin(th3)) * 1000.0
                z_calc = (self.L_base + self.L_shoulder * math.cos(th1) + self.L_elbow * math.cos(th2) + self.L_tool * math.cos(th3)) * 1000.0

                dr = r_work - r_calc
                dz = z_work - z_calc
                err = math.hypot(dr, dz)

                if err < best_err:
                    best_err = err
                    best_q = list(q)

                if err < 0.05:
                    break

                c1 = self.L_shoulder * math.cos(th1) * 1000.0
                c2 = self.L_elbow * math.cos(th2) * 1000.0
                c3 = self.L_tool * math.cos(th3) * 1000.0
                s1 = -self.L_shoulder * math.sin(th1) * 1000.0
                s2 = -self.L_elbow * math.sin(th2) * 1000.0
                s3 = -self.L_tool * math.sin(th3) * 1000.0

                j00 = -(c1 + c2 + c3) * deg2rad
                j01 = -(c2 + c3) * deg2rad
                j02 = c3 * deg2rad
                j10 = -(s1 + s2 + s3) * deg2rad
                j11 = -(s2 + s3) * deg2rad
                j12 = s3 * deg2rad

                lam = 0.4
                a00 = j00*j00 + j01*j01 + j02*j02 + lam*lam
                a01 = j00*j10 + j01*j11 + j02*j12
                a10 = a01
                a11 = j10*j10 + j11*j11 + j12*j12 + lam*lam

                det = a00 * a11 - a01 * a10
                if abs(det) < 1e-12:
                    break

                inv00 = a11 / det
                inv01 = -a01 / det
                inv10 = -a10 / det
                inv11 = a00 / det

                v0 = inv00 * dr + inv01 * dz
                v1 = inv10 * dr + inv11 * dz

                dq0 = j00 * v0 + j10 * v1
                dq1 = j01 * v0 + j11 * v1
                dq2 = j02 * v0 + j12 * v1

                step_len = math.hypot(dq0, dq1, dq2)
                if step_len > 15.0:
                    scale = 15.0 / step_len
                    dq0 *= scale
                    dq1 *= scale
                    dq2 *= scale

                q[0] = max(5.0, min(175.0, q[0] + dq0))
                q[1] = max(5.0, min(175.0, q[1] + dq1))
                q[2] = max(5.0, min(175.0, q[2] + dq2))

            if best_err < 0.1:
                break

        if best_q is None:
            best_q = [90.0, 90.0, 90.0]

        wrist_roll_deg = 90.0
        claw_deg = cur_angles[5] if (cur_angles and len(cur_angles) >= 6) else 20.0

        solved_angles = [
            round(base_deg, 2),
            round(best_q[0], 2),
            round(best_q[1], 2),
            round(best_q[2], 2),
            round(wrist_roll_deg, 2),
            round(claw_deg, 2)
        ]

        fk_check = self.calculate_forward_kinematics(solved_angles)
        actual_pos = fk_check["end_effector"]
        error_mm = math.hypot(
            actual_pos[0] - tx,
            actual_pos[1] - ty,
            actual_pos[2] - tz
        )

        return {
            "reachable": reachable and (error_mm < 25.0),
            "target": [round(tx, 2), round(ty, 2), round(tz, 2)],
            "solved_angles": solved_angles,
            "achieved_coords": actual_pos,
            "position_error_mm": round(error_mm, 2)
        }

    def calculate_gravity_torques(self, angles_deg: List[float]) -> Dict[str, Any]:
        if len(angles_deg) < 6:
            angles_deg = list(angles_deg) + [90.0] * (6 - len(angles_deg))

        q_sh = math.radians(angles_deg[1] - 90.0)
        q_el = q_sh + math.radians(angles_deg[2] - 90.0)
        q_wr = q_el + math.radians(angles_deg[3] - 90.0)

        m_tool = self.m_gripper + self.payload_mass
        m_wrist_total = self.m_wrist + m_tool

        d_com1_h = self.L_shoulder * self.r_com_shoulder * math.sin(q_sh)
        d_L1_h = self.L_shoulder * math.sin(q_sh)
        d_com2_h = self.L_elbow * self.r_com_elbow * math.sin(q_el)
        d_L2_h = self.L_elbow * math.sin(q_el)
        d_com3_h = self.L_tool * self.r_com_wrist * math.sin(q_wr)

        tau_wrist = (self.m_wrist * self.r_com_wrist * self.L_wrist + m_tool * (self.L_wrist + self.L_claw * 0.5)) * self.g * math.sin(q_wr)

        tau_elbow = (self.m_elbow * self.r_com_elbow * self.L_elbow * math.sin(q_el) +
                     m_wrist_total * (self.L_elbow * math.sin(q_el) + d_com3_h)) * self.g

        tau_shoulder = (self.m_shoulder * self.r_com_shoulder * self.L_shoulder * math.sin(q_sh) +
                        self.m_elbow * (d_L1_h + d_com2_h) +
                        m_wrist_total * (d_L1_h + d_L2_h + d_com3_h)) * self.g

        torques_nm = [0.0, round(tau_shoulder, 3), round(tau_elbow, 3), round(tau_wrist, 3), 0.0, 0.0]

        droop_deg = [0.0] * 6
        compensated_angles = list(angles_deg)

        for i in range(6):
            if self.k_stiffness[i] > 0:
                deflection = torques_nm[i] / self.k_stiffness[i]
                droop_deg[i] = round(deflection, 2)
                if self.compensation_enabled:
                    offset = -deflection * self.compensation_gain
                    compensated_angles[i] = round(max(0.0, min(180.0, angles_deg[i] + offset)), 2)

        load_pct = [min(100.0, round((abs(torques_nm[i]) / self.max_torque[i]) * 100.0, 1)) for i in range(6)]

        return {
            "torques_nm": torques_nm,
            "droop_deg": droop_deg,
            "compensated_angles": compensated_angles,
            "load_pct": load_pct,
            "payload_mass_kg": round(self.payload_mass, 3),
            "payload_mass_g": round(self.payload_mass * 1000.0, 1),
            "compensation_enabled": self.compensation_enabled,
            "compensation_gain": round(self.compensation_gain, 2)
        }

    def generate_joint_trajectory(self, start_angles: List[float], goal_angles: List[float],
                                  duration_sec: float = 1.5, num_steps: int = 25) -> List[Dict[str, Any]]:
        trajectory = []
        dt = duration_sec / max(1, num_steps)

        for step in range(num_steps + 1):
            t_norm = step / float(num_steps)
            s = 10.0 * (t_norm ** 3) - 15.0 * (t_norm ** 4) + 6.0 * (t_norm ** 5)
            s_dot = (30.0 * (t_norm ** 2) - 60.0 * (t_norm ** 3) + 30.0 * (t_norm ** 4)) / max(0.001, duration_sec)

            step_angles = []
            step_velocities = []

            for j in range(6):
                th = start_angles[j] + (goal_angles[j] - start_angles[j]) * s
                omega = (goal_angles[j] - start_angles[j]) * s_dot
                step_angles.append(round(th, 2))
                step_velocities.append(round(omega, 2))

            fk = self.calculate_forward_kinematics(step_angles)

            trajectory.append({
                "time_sec": round(step * dt, 3),
                "angles": step_angles,
                "velocities": step_velocities,
                "end_effector": fk["end_effector"]
            })

        return trajectory

    def plan_safe_homing_path(self, current_angles: List[float]) -> List[Dict[str, Any]]:
        cur = list(current_angles)
        fk_cur = self.calculate_forward_kinematics(cur)
        cur_z = fk_cur["end_effector"][2]

        waypoints = []

        if cur_z < 300.0 or cur[1] < 80.0 or cur[2] < 80.0:
            lift_pose = [cur[0], 90.00, 90.00, 90.00, 90.00, 20.00]
            waypoints.append({
                "phase": "clearance_lift",
                "description": "Vertical Clearance Lift (Prevent Table Collision)",
                "target_angles": lift_pose,
                "duration": 1.2
            })
            start_for_slew = lift_pose
        else:
            start_for_slew = cur

        home_pose = list(self.HOME_ANGLES)
        waypoints.append({
            "phase": "home_lock",
            "description": "Slew Base to Center & Lock Home (0, 0, 420mm)",
            "target_angles": home_pose,
            "duration": 1.4
        })

        return {
            "start_angles": cur,
            "home_angles": self.HOME_ANGLES,
            "home_coords": self.HOME_COORDS,
            "waypoints": waypoints
        }

    def compute_full_physics_frame(self, current_angles: List[float], target_angles: List[float]) -> Dict[str, Any]:
        fk = self.calculate_forward_kinematics(current_angles)
        grav = self.calculate_gravity_torques(current_angles)

        return {
            "fk": fk,
            "gravity": grav,
            "current_angles": [round(a, 2) for a in current_angles],
            "target_angles": [round(a, 2) for a in target_angles],
            "home_coords": self.HOME_COORDS
        }
