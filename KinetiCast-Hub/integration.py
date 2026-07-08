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

def calculate_mag_heading(mx, my, offset_x, offset_y):
    """地磁気センサ値から校正済みの絶対方位(度)を算出"""
    calibrated_mx = mx - offset_x
    calibrated_my = my - offset_y
    heading_rad = math.atan2(calibrated_my, calibrated_mx)
    heading_deg = math.degrees(heading_rad)
    if heading_deg < 0:
        heading_deg += 360.0
    return heading_deg

# ---- ZUPT静止判定の閾値 ----
ZUPT_ACCEL_TOLERANCE = 0.03  # G単位
ZUPT_GYRO_THRESHOLD = 3.0    # deg/s単位

# 🧭 地磁気相補フィルターゲイン（ジャイロのYawドリフトを引き戻す強度。0.02 = 毎ステップ誤差の2%を強制補正）
MAG_FEEDBACK_GAIN = 0.02


def integrate_batch(samples, state, mag_offset_x=130.0, mag_offset_y=-350.0):
    """
    9軸（重力キャリブレーション + ジャイロ積分 + 地磁気コンパスロック）を用いた航法計算
    """
    if "gravity_mag" not in state or state["gravity_mag"] is None:
        state["calib_buffer"].extend(samples)
        if len(state["calib_buffer"]) >= 50:
            g0 = calibrate_gravity(state["calib_buffer"], 50)
            g_mag = math.sqrt(g0[0]**2 + g0[1]**2 + g0[2]**2)
            state["gravity_mag"] = g_mag
            
            # 💡【修正1】初期の静止重力ベクトル（＝上向きの比力1G）を、世界座標系の「垂直上向き（+g_mag）」にマッピングします
            state["quat"] = get_rotation_quat(g0, (0.0, 0.0, g_mag))

            # 🧭 初期方位(Yaw)を地磁気の絶対方位に合わせ込む
            init_s = state["calib_buffer"][0]
            mag_heading = calculate_mag_heading(init_s["mx"], init_s["my"], mag_offset_x, mag_offset_y)
            mag_heading_rad = math.radians(mag_heading)
            q_yaw_init = (math.cos(-mag_heading_rad / 2.0), 0.0, 0.0, math.sin(-mag_heading_rad / 2.0))
            state["quat"] = quat_normalize(quat_mult(q_yaw_init, state["quat"]))

            print(f"🟢 [CALIBRATION] Gravity Mag: {g_mag:.4f} G")
            print(f"🧭 [CALIBRATION] Initial Compass Orientation Locked: {mag_heading:.1f}°")
        else:
            for s in samples:
                s["vx"] = s["vy"] = s["vz"] = 0.0
                s["px"] = s["py"] = s["pz"] = 0.0
                s["heading"] = 0.0
            return state

    g_mag = state["gravity_mag"]
    q = state["quat"]
    vx, vy, vz = state["vx"], state["vy"], state["vz"]
    px, py, pz = state["px"], state["py"], state["pz"]
    t_prev = state["last_t"]

    zupt_count = 0

    for s in samples:
        t = s["t"] / 1000.0
        dt = (t - t_prev) if t_prev is not None else 0.01
        if dt <= 0:
            dt = 0.01

        # 1. 姿勢更新 (ジャイロ積分)
        wx = math.radians(s["gx"])
        wy = math.radians(s["gy"])
        wz = math.radians(s["gz"])
        omega_q = (0.0, wx, wy, wz)
        dq = quat_mult(q, omega_q)
        q = tuple(qi + 0.5 * dqi * dt for qi, dqi in zip(q, dq))
        q = quat_normalize(q)

        # 🧭 2. 地磁気コンパスによる Yaw ドリフトの絶対補正
        mag_heading = calculate_mag_heading(s["mx"], s["my"], mag_offset_x, mag_offset_y)
        qw, qx, qy, qz = q
        current_yaw_rad = math.atan2(2.0 * (qw * qz + qx * qy), 1.0 - 2.0 * (qy * qy + qz * qz))
        current_yaw_deg = math.degrees(current_yaw_rad)
        if current_yaw_deg < 0:
            current_yaw_deg += 360.0

        heading_error = mag_heading - current_yaw_deg
        if heading_error > 180.0: heading_error -= 360.0
        elif heading_error < -180.0: heading_error += 360.0

        correction_angle = math.radians(heading_error) * MAG_FEEDBACK_GAIN
        q_corr = (math.cos(correction_angle / 2.0), 0.0, 0.0, math.sin(correction_angle / 2.0))
        q = quat_normalize(quat_mult(q_corr, q))

        # 3. 加速度を世界座標系に変換
        a_body = (s["ax"], s["ay"], s["az"])
        a_world = rotate_vector(q, a_body)

        # 💡【修正2】世界座標系（上向きがプラス）において、地球の重力による影響（1G分）を引き算します
        ax_ms2 = a_world[0] * 9.80665
        ay_ms2 = a_world[1] * 9.80665
        az_ms2 = (a_world[2] - g_mag) * 9.80665  # 👈 「+」から「-」へ修正

        # 5. 速度・位置の積分
        vx += ax_ms2 * dt
        vy += ay_ms2 * dt
        vz += az_ms2 * dt

        # ---- 6. ZUPT: 静止判定できたら速度を強制ゼロへ ----
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

        # 各サンプルデータに航法計算結果をマージ
        s["vx"], s["vy"], s["vz"] = vx, vy, vz
        s["px"], s["py"], s["pz"] = px, py, pz
        s["qw"], s["qx"], s["qy"], s["qz"] = q
        s["heading"] = current_yaw_deg
        s["zupt"] = is_stationary

        t_prev = t

    state.update({
        "quat": q,
        "vx": vx, "vy": vy, "vz": vz,
        "px": px, "py": py, "pz": pz,
        "last_t": t_prev
    })
    state["_zupt_count_debug"] = zupt_count
    return state
