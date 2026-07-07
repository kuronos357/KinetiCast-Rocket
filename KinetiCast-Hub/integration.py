# integration.py (修正版)
import math

def quat_mult(q1, q2):
    w1, x1, y1, z1 = q1
    w2, x2, y2, z2 = q2
    return (
        w1*w2 - x1*x2 - y1*y2 - z1*z2,
        w1*x2 + x1*w2 + y1*z2 - z1*y2,
        w1*y2 - x1*z2 + y1*w2 + z1*x2,
        w1*z2 + x1*y2 - y1*x2 + z1*w2
    )

def quat_conjugate(q):
    w, x, y, z = q
    return (w, -x, -y, -z)

def quat_normalize(q):
    w, x, y, z = q
    n = math.sqrt(w*w + x*x + y*y + z*z)
    if n == 0:
        return (1.0, 0.0, 0.0, 0.0)
    return (w/n, x/n, y/n, z/n)

def rotate_vector(q, v):
    """クォータニオンqでベクトルvを回転"""
    vq = (0.0, v[0], v[1], v[2])
    r = quat_mult(quat_mult(q, vq), quat_conjugate(q))
    return (r[1], r[2], r[3])

def get_rotation_quat(v_from, v_to):
    """v_fromベクトルをv_toベクトルへ回転させる最小回転のクォータニオンを計算"""
    mag_from = math.sqrt(sum(x**2 for x in v_from))
    mag_to = math.sqrt(sum(x**2 for x in v_to))
    if mag_from == 0 or mag_to == 0:
        return (1.0, 0.0, 0.0, 0.0)
    
    u = [x / mag_from for x in v_from]
    v = [x / mag_to for x in v_to]
    
    dot = sum(ui*vi for ui, vi in zip(u, v))
    if dot > 0.99999:
        return (1.0, 0.0, 0.0, 0.0)
    if dot < -0.99999:
        # 真逆（180度）の場合は直交する任意の軸で反転
        axis = (0.0, -u[2], u[1]) if abs(u[0]) < 0.8 else (-u[1], u[0], 0.0)
        axis_mag = math.sqrt(sum(x**2 for x in axis))
        return (0.0, axis[0]/axis_mag, axis[1]/axis_mag, axis[2]/axis_mag)
        
    w = (
        u[1]*v[2] - u[2]*v[1],
        u[2]*v[0] - u[0]*v[2],
        u[0]*v[1] - u[1]*v[0]
    )
    return quat_normalize((1.0 + dot, w[0], w[1], w[2]))

def calibrate_gravity(samples, num_samples=50):
    n = min(num_samples, len(samples))
    gx = sum(s["ax"] for s in samples[:n]) / n
    gy = sum(s["ay"] for s in samples[:n]) / n
    gz = sum(s["az"] for s in samples[:n]) / n
    return (gx, gy, gz)

def integrate_batch(samples, state):
    """
    state = {
        "gravity_mag": float or None,         # 起動時に測定した重力の大きさ(G単位)
        "calib_buffer": list,
        "quat": (w,x,y,z),                    # 世界座標系（Z軸が鉛直真上）に対する機体の姿勢
        "vx","vy","vz","px","py","pz": float,
        "last_t": float or None
    }
    """
    if "gravity_mag" not in state or state["gravity_mag"] is None:
        state["calib_buffer"].extend(samples)
        if len(state["calib_buffer"]) >= 50:
            g0 = calibrate_gravity(state["calib_buffer"], 50)
            g_mag = math.sqrt(g0[0]**2 + g0[1]**2 + g0[2]**2)
            state["gravity_mag"] = g_mag
            
            # 【ここが重要】機体座標系の重力(g0)が、世界座標系の真下 (0, 0, -g_mag) に
            # 一致するような初期姿勢クォータニオンを計算（これで世界座標のZ軸が鉛直真上になる）
            state["quat"] = get_rotation_quat(g0, (0.0, 0.0, -g_mag))
            
            print(f"[CALIBRATION] Detected Gravity Mag: {g_mag:.4f} G")
            print(f"[CALIBRATION] Initial Quat (World Align): {state['quat']}")
        else:
            for s in samples:
                s["vx"] = s["vy"] = s["vz"] = 0.0
                s["px"] = s["py"] = s["pz"] = 0.0
            return state

    g_mag = state["gravity_mag"]
    q = state["quat"]
    vx, vy, vz = state["vx"], state["vy"], state["vz"]
    px, py, pz = state["px"], state["py"], state["pz"]
    t_prev = state["last_t"]

    for s in samples:
        t = s["t"] / 1000.0
        dt = (t - t_prev) if t_prev is not None else 0.01
        if dt <= 0:
            dt = 0.01

        # 1. ジャイロ(deg/s)からクォータニオンの更新
        wx = math.radians(s["gx"])
        wy = math.radians(s["gy"])
        wz = math.radians(s["gz"])

        omega_q = (0.0, wx, wy, wz)
        dq = quat_mult(q, omega_q)
        q = tuple(qi + 0.5 * dqi * dt for qi, dqi in zip(q, dq))
        q = quat_normalize(q)

        # 2. 【キモ】機体座標系の加速度を、現在の姿勢を使って世界座標系に変換
        a_body = (s["ax"], s["ay"], s["az"])
        a_world = rotate_vector(q, a_body)

        # 3. 世界座標系（Z軸＝鉛直上向き）で重力加速度を除去
        # 世界座標では重力は常に (0, 0, -g_mag) なので、Z軸に g_mag を足せば相殺される
        ax_ms2 = a_world[0] * 9.80665
        ay_ms2 = a_world[1] * 9.80665
        az_ms2 = (a_world[2] + g_mag) * 9.80665  # -(-g_mag) なので足し算

        # 4. 世界座標系での2階積分
        vx += ax_ms2 * dt
        vy += ay_ms2 * dt
        vz += az_ms2 * dt
        px += vx * dt
        py += vy * dt
        pz += vz * dt

        # データを保存
        s["vx"], s["vy"], s["vz"] = vx, vy, vz
        s["px"], s["py"], s["pz"] = px, py, pz
        s["qw"], s["qx"], s["qy"], s["qz"] = q

        t_prev = t

    state.update({
        "quat": q,
        "vx": vx, "vy": vy, "vz": vz,
        "px": px, "py": py, "pz": pz,
        "last_t": t_prev
    })
    return state