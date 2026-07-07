# integration.py (全面書き換え)
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


def calibrate_gravity(samples, num_samples=50):
    n = min(num_samples, len(samples))
    gx = sum(s["ax"] for s in samples[:n]) / n
    gy = sum(s["ay"] for s in samples[:n]) / n
    gz = sum(s["az"] for s in samples[:n]) / n
    return (gx, gy, gz)


def integrate_batch(samples, state):
    """
    state = {
        "gravity_ref": (gx,gy,gz) or None,   # 起動時に測定した重力ベクトル(機体座標系)
        "calib_buffer": list,
        "quat": (w,x,y,z),                    # 起動時姿勢を基準とした相対回転
        "vx","vy","vz","px","py","pz": float,
        "last_t": float or None
    }
    """
    if state["gravity_ref"] is None:
        state["calib_buffer"].extend(samples)
        if len(state["calib_buffer"]) >= 50:
            g0 = calibrate_gravity(state["calib_buffer"], 50)
            state["gravity_ref"] = g0
            state["quat"] = (1.0, 0.0, 0.0, 0.0)  # 初期姿勢=基準(単位クォータニオン)
            print(f"[CALIBRATION] gravity vector (body frame @ t0): {g0}")
        else:
            for s in samples:
                s["vx"] = s["vy"] = s["vz"] = 0.0
                s["px"] = s["py"] = s["pz"] = 0.0
            return state

    g0 = state["gravity_ref"]
    q = state["quat"]
    vx, vy, vz = state["vx"], state["vy"], state["vz"]
    px, py, pz = state["px"], state["py"], state["pz"]
    t_prev = state["last_t"]

    for s in samples:
        t = s["t"] / 1000.0
        dt = (t - t_prev) if t_prev is not None else 0.01
        if dt <= 0:
            dt = 0.01

        # ジャイロ(deg/s)をrad/sに変換
        wx = math.radians(s["gx"])
        wy = math.radians(s["gy"])
        wz = math.radians(s["gz"])

        # クォータニオンの微分方程式: dq/dt = 0.5 * q ⊗ [0, wx, wy, wz]
        omega_q = (0.0, wx, wy, wz)
        dq = quat_mult(q, omega_q)
        q = tuple(qi + 0.5 * dqi * dt for qi, dqi in zip(q, dq))
        q = quat_normalize(q)

        # 起動時の重力ベクトルを、現在の姿勢(逆回転)で現在の機体座標系に投影
        q_inv = quat_conjugate(q)
        g_body = rotate_vector(q_inv, g0)

        ax_ms2 = (s["ax"] - g_body[0]) * 9.80665
        ay_ms2 = (s["ay"] - g_body[1]) * 9.80665
        az_ms2 = (s["az"] - g_body[2]) * 9.80665

        if not is_stationary_debug_printed_once:
            print(f"[DEBUG] raw accel after gravity removal: "
                f"ax={ax_ms2:.4f} ay={ay_ms2:.4f} az={az_ms2:.4f} (should be ~0 if stationary)")

        vx += ax_ms2 * dt
        vy += ay_ms2 * dt
        vz += az_ms2 * dt
        px += vx * dt
        py += vy * dt
        pz += vz * dt

        s["vx"], s["vy"], s["vz"] = vx, vy, vz
        s["px"], s["py"], s["pz"] = px, py, pz
        s["qw"], s["qx"], s["qy"], s["qz"] = q  # 姿勢そのものも保存

        t_prev = t


    state.update({
        "quat": q,
        "vx": vx, "vy": vy, "vz": vz,
        "px": px, "py": py, "pz": pz,
        "last_t": t_prev
    })
    return state