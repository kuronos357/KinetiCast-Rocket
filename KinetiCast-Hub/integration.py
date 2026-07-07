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
    vq = (0.0, v[0], v[1], v[2])
    r = quat_mult(quat_mult(q, vq), quat_conjugate(q))
    return (r[1], r[2], r[3])

def get_rotation_quat(v_from, v_to):
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


# ---- ZUPT判定の閾値 ----
ZUPT_ACCEL_TOLERANCE = 0.03  # 重力の大きさからのズレがこの範囲内なら静止とみなす(G単位)
ZUPT_GYRO_THRESHOLD = 3.0    # 角速度がこの範囲内なら静止とみなす(deg/s)


def integrate_batch(samples, state):
    """
    state = {
        "gravity_mag": float or None,
        "calib_buffer": list,
        "quat": (w,x,y,z),
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
            state["quat"] = get_rotation_quat(g0, (0.0, 0.0, -g_mag))

            # 検証用:キャリブレーションが正しく揃っているか確認
            check = rotate_vector(state["quat"], g0)
            print(f"[CALIBRATION] Gravity Mag: {g_mag:.4f} G")
            print(f"[CALIBRATION] Verify rotated g0={check} (should be ~(0,0,{-g_mag:.4f}))")
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

    zupt_count = 0  # このバッチ内で何回ZUPTが発動したかのカウンタ(デバッグ用)

    for s in samples:
        t = s["t"] / 1000.0
        dt = (t - t_prev) if t_prev is not None else 0.01
        if dt <= 0:
            dt = 0.01

        # 1. 姿勢更新(ジャイロ積分)
        wx = math.radians(s["gx"])
        wy = math.radians(s["gy"])
        wz = math.radians(s["gz"])
        omega_q = (0.0, wx, wy, wz)
        dq = quat_mult(q, omega_q)
        q = tuple(qi + 0.5 * dqi * dt for qi, dqi in zip(q, dq))
        q = quat_normalize(q)

        # 2. 加速度を世界座標系に変換
        a_body = (s["ax"], s["ay"], s["az"])
        a_world = rotate_vector(q, a_body)

        # 3. 世界座標系で重力を除去
        ax_ms2 = a_world[0] * 9.80665
        ay_ms2 = a_world[1] * 9.80665
        az_ms2 = (a_world[2] + g_mag) * 9.80665

        # 4. 速度・位置の積分
        vx += ax_ms2 * dt
        vy += ay_ms2 * dt
        vz += az_ms2 * dt

        # ---- 5. ZUPT: 静止判定できたら速度を強制ゼロへ ----
        accel_mag = math.sqrt(s["ax"]**2 + s["ay"]**2 + s["az"]**2)
        gyro_mag = math.sqrt(s["gx"]**2 + s["gy"]**2 + s["gz"]**2)
        is_stationary = (
            abs(accel_mag - g_mag) < ZUPT_ACCEL_TOLERANCE and
            gyro_mag < ZUPT_GYRO_THRESHOLD
        )
        if is_stationary:
            vx, vy, vz = 0.0, 0.0, 0.0
            zupt_count += 1

        px += vx * dt
        py += vy * dt
        pz += vz * dt

        s["vx"], s["vy"], s["vz"] = vx, vy, vz
        s["px"], s["py"], s["pz"] = px, py, pz
        s["qw"], s["qx"], s["qy"], s["qz"] = q
        s["zupt"] = is_stationary  # フロント側でも「静止判定中か」を確認できるように

        t_prev = t

    state.update({
        "quat": q,
        "vx": vx, "vy": vy, "vz": vz,
        "px": px, "py": py, "pz": pz,
        "last_t": t_prev
    })
    state["_zupt_count_debug"] = zupt_count  # main.py側でログ表示用
    return state