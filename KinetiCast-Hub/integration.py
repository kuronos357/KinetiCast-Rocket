import numpy as np
from scipy.spatial.transform import Rotation as R

class MadgwickAHRS:
    def __init__(self, sample_period=0.01, beta=0.2):
        self.sample_period = sample_period
        self.beta = beta
        self.q = np.array([1.0, 0.0, 0.0, 0.0]) # [w, x, y, z]

    def update_6dof(self, gx, gy, gz, ax, ay, az):
        q1, q2, q3, q4 = self.q
        
        norm = np.sqrt(ax**2 + ay**2 + az**2)
        if norm == 0: return
        ax, ay, az = ax / norm, ay / norm, az / norm

        f1 = 2.0 * (q2*q4 - q1*q3) - ax
        f2 = 2.0 * (q1*q2 + q3*q4) - ay
        f3 = 2.0 * (0.5 - q2**2 - q3**2) - az
        
        j_11_24 = 2.0 * q3
        j_12_23 = 2.0 * q4
        j_13_22 = 2.0 * q1
        j_14_21 = 2.0 * q2
        j_32 = 2.0 * j_14_21
        j_33 = 2.0 * j_13_22
        
        step_x = j_11_24 * f1 - j_13_22 * f2
        step_y = j_12_23 * f1 + j_14_21 * f2 - j_32 * f3
        step_z = j_12_23 * f2 + j_14_21 * f1 - j_33 * f3
        step_w = j_11_24 * f2 + j_13_22 * f1

        norm = np.sqrt(step_x**2 + step_y**2 + step_z**2 + step_w**2)
        if norm == 0: return
        step_x, step_y, step_z, step_w = step_x / norm, step_y / norm, step_z / norm, step_w / norm

        q_dot_w = 0.5 * (-q2*gx - q3*gy - q4*gz) - self.beta * step_w
        q_dot_x = 0.5 * ( q1*gx + q3*gz - q4*gy) - self.beta * step_x
        q_dot_y = 0.5 * ( q1*gy - q2*gz + q4*gx) - self.beta * step_y
        q_dot_z = 0.5 * ( q1*gz + q2*gy - q3*gx) - self.beta * step_z

        self.q += np.array([q_dot_w, q_dot_x, q_dot_y, q_dot_z]) * self.sample_period
        norm = np.sqrt(self.q[0]**2 + self.q[1]**2 + self.q[2]**2 + self.q[3]**2)
        self.q /= norm

filter_instance = None
is_initialized = False

def integrate_batch(batch_samples, nav_state, mag_offset_x=0, mag_offset_y=0):
    global filter_instance, is_initialized
    
    v = np.array([nav_state["vx"], nav_state["vy"], nav_state["vz"]])
    p = np.array([nav_state["px"], nav_state["py"], nav_state["pz"]])
    last_t = nav_state["last_t"]
    calib_buffer = nav_state.get("calib_buffer", [])
    
    if filter_instance is None:
        filter_instance = MadgwickAHRS(sample_period=0.01, beta=0.2)

    G_TO_MS2 = 9.80665

    for sample in batch_samples:
        t_sec = sample["t"] / 1000.0
        
        if last_t is None:
            last_t = t_sec
            sample["px"], sample["py"], sample["pz"] = p[0], p[1], p[2]
            sample["vx"], sample["vy"], sample["vz"] = v[0], v[1], v[2]
            continue
            
        dt = t_sec - last_t
        if dt <= 0 or dt > 0.5: dt = 0.01
        last_t = t_sec

        acc_body = np.array([sample["ax"], sample["ay"], sample["az"]])
        gx_rad = np.radians(sample["gx"])
        gy_rad = np.radians(sample["gy"])
        gz_rad = np.radians(sample["gz"])

        # 1. 自動姿勢初期化 (最初の50行で水平面を自動定義)
        if not is_initialized:
            calib_buffer.append(acc_body)
            if len(calib_buffer) >= 50:
                avg_acc = np.mean(calib_buffer, axis=0)
                gravity_mag = np.linalg.norm(avg_acc)
                
                z_axis = avg_acc / gravity_mag
                r_init, _ = R.align_vectors([[0, 0, 1]], [z_axis])
                q_init = r_init.as_quat() # [x, y, z, w]
                
                filter_instance.q = np.array([q_init[3], q_init[0], q_init[1], q_init[2]])
                is_initialized = True
                print(f"🟢 INS Initialized! Ground Base Lock.")
                
            sample["px"], sample["py"], sample["pz"] = p[0], p[1], p[2]
            sample["vx"], sample["vy"], sample["vz"] = v[0], v[1], v[2]
            continue

        # 2. 姿勢更新
        filter_instance.update_6dof(gx_rad, gy_rad, gz_rad, acc_body[0], acc_body[1], acc_body[2])
        
        q_scipy = [filter_instance.q[1], filter_instance.q[2], filter_instance.q[3], filter_instance.q[0]]
        r = R.from_quat(q_scipy)

        # 3. 座標変換と重力相殺 (どんな姿勢になっても常に世界座標のZ軸から1Gを引く)
        acc_body_ms2 = acc_body * G_TO_MS2
        acc_world = r.apply(acc_body_ms2)
        acc_pure = acc_world - np.array([0.0, 0.0, G_TO_MS2])

        # 4. ZUPT (静止判定時は速度を強制ゼロにして発散を完全防御)
        acc_mag = np.linalg.norm(acc_body)
        gyro_mag = np.sqrt(sample["gx"]**2 + sample["gy"]**2 + sample["gz"]**2)
        
        if abs(acc_mag - 1.0) < 0.04 and gyro_mag < 5.0:
            v = np.array([0.0, 0.0, 0.0])
        else:
            v += acc_pure * dt
            
        p += v * dt

        sample["px"], sample["py"], sample["pz"] = p[0], p[1], p[2]
        sample["vx"], sample["vy"], sample["vz"] = v[0], v[1], v[2]

    nav_state["quat"] = [filter_instance.q[1], filter_instance.q[2], filter_instance.q[3], filter_instance.q[0]]
    nav_state["vx"], nav_state["vy"], nav_state["vz"] = v[0], v[1], v[2]
    nav_state["px"], nav_state["py"], nav_state["pz"] = p[0], p[1], p[2]
    nav_state["last_t"] = last_t
    nav_state["calib_buffer"] = calib_buffer

    return nav_state