import socket
import struct
import time
import datetime
import os
import sys  # 💡 画面上書き用の制御に必要
import firebase_admin
from firebase_admin import credentials, firestore
import integration  # 💡 新しくなった航法モジュールをインポート

# ==========================================
# 1. ネットワーク & Firebase 設定
# ==========================================
UDP_IP = "0.0.0.0"
UDP_PORT = 9870
CRED_PATH = "0_Project\\KinetiCast-Rocket\\KinetiCast-Hub\\secrets\\kineticast-firebase-adminsdk.json"

try:
    cred = credentials.Certificate(CRED_PATH)
    firebase_admin.initialize_app(cred)
    db = firestore.client()
    print("🟢 Firebase Admin SDK initialized successfully. Production Mode Active.")
except Exception as e:
    print(f"🔴 Firebase initialization failed: {e}")
    exit(1)

# ==========================================
# 2. テレメトリ解析・バッチ計算用内部状態(State)
# ==========================================
PACKET_FORMAT = '<Ifffffffff'
PACKET_SIZE = 40
BATCH_SIZE_LIMIT = 100

MAG_OFFSET_X = 130.0
MAG_OFFSET_Y = -350.0

# 慣性航法の連続性を維持するための状態辞書
nav_state = {
    "gravity_mag": None,
    "calib_buffer": [],
    "quat": (1.0, 0.0, 0.0, 0.0),
    "vx": 0.0, "vy": 0.0, "vz": 0.0,
    "px": 0.0, "py": 0.0, "pz": 0.0,
    "last_t": None
}

# ==========================================
# 3. UDP 受信・リアルタイムセッションループ
# ==========================================
def main():
    global nav_state
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024 * 1024)
    
    print(f"📦 KinetiCast 9-Axis Backend [9軸融合モード] Active. Listening on {UDP_IP}:{UDP_PORT} ...")

    # 🚀 起動ごとに日時に基づくユニークな親セッションIDを生成
    session_id = datetime.datetime.now().strftime("session_%Y%m%d_%H%M%S")
    session_ref = db.collection("sessions").document(session_id)
    
    print(f"📡 New Flight Session Created in Firestore: {session_id}")
    session_ref.set({
        "createdAt": firestore.SERVER_TIMESTAMP,
        "status": "LIVE",
        "imageUrl": None
    })

    # ループ突入前に一度コンソール画面を完全にクリアする
    os.system('cls' if os.name == 'nt' else 'clear')

    batch_samples = []
    chunk_index = 0
    packet_count = 0  # 💡 画面更新のタイミングを間引くためのカウンター

    while True:
        try:
            data, addr = sock.recvfrom(1024)
            if len(data) != PACKET_SIZE:
                continue
                
            parsed = struct.unpack(PACKET_FORMAT, data)
            t_ms, ax, ay, az, gx, gy, gz, mx, my, mz = parsed
            
            # 💡 Viewer.tsx の TelemetrySample が直接マッピングして読めるフラットなキー構造で格納
            sample_node = {
                "t": t_ms,
                "ax": ax, "ay": ay, "az": az,
                "gx": gx, "gy": gy, "gz": gz,
                "mx": mx, "my": my, "mz": mz
            }
            batch_samples.append(sample_node)
            
            # --------------------------------------------------
            # 🖥️ リアルタイム・コンソールダッシュボード表示 (5パケットごとに上書き)
            # --------------------------------------------------
            packet_count += 1
            if packet_count % 5 == 0:
                # "\033[H" でコンソールのカーソルを一番左上にワープさせます (スクロールしない)
                sys.stdout.write("\033[H")
                sys.stdout.write(f"==================================================\n")
                sys.stdout.write(f"🚀 KINETICAST TELEMETRY STREAMING | Session: {session_id}\n")
                sys.stdout.write(f"==================================================\n")
                sys.stdout.write(f" ⏱️ BOARD TIME : {t_ms:<10} ms\n")
                sys.stdout.write(f" 📡 UDP PACKETS: {packet_count:<10} pkts\n")
                sys.stdout.write(f"--------------------------------------------------\n")
                sys.stdout.write(f" 🔹 RAW SENSOR VALUES (Latest)\n")
                sys.stdout.write(f"  ACCEL (G) : X={ax:+7.3f}, Y={ay:+7.3f}, Z={az:+7.3f}\n")
                sys.stdout.write(f"  GYRO (d/s): X={gx:+7.3f}, Y={gy:+7.3f}, Z={gz:+7.3f}\n")
                sys.stdout.write(f"  MAG (uT)  : X={mx:+7.3f}, Y={my:+7.3f}, Z={mz:+7.3f}\n")
                sys.stdout.write(f"--------------------------------------------------\n")
                sys.stdout.write(f" 🛰️ NAVIGATION REALTIME STATE (Calculated)\n")
                sys.stdout.write(f"  POSITION(m): X={nav_state['px']:+8.2f}, Y={nav_state['py']:+8.2f}, Z={nav_state['pz']:+8.2f}\n")
                sys.stdout.write(f"  VELOCITY(m): X={nav_state['vx']:+8.2f}, Y={nav_state['vy']:+8.2f}, Z={nav_state['vz']:+8.2f}\n")
                sys.stdout.write(f"--------------------------------------------------\n")
                sys.stdout.write(f" 🔥 FIRESTORE STATUS\n")
                sys.stdout.write(f"  Chunks Pushed: {chunk_index:<5} | ZUPT Loops: {nav_state.get('_zupt_count_debug', 0):<5}\n")
                sys.stdout.write(f"==================================================\n")
                sys.stdout.flush()

            # 100サンプル（約1秒分）貯まったら一括航法処理を実行して子コレクションへ送信
            if len(batch_samples) >= BATCH_SIZE_LIMIT:
                # 🚀 integration.py を駆動。batch_samplesの中身が参照渡しで直接、位置・速度・地磁気方位で上書きされます
                nav_state = integration.integrate_batch(batch_samples, nav_state, MAG_OFFSET_X, MAG_OFFSET_Y)
                
                # パス構造: sessions/{session_id}/chunks/chunk_000xx
                chunk_id = f"chunk_{chunk_index:05d}"
                chunk_ref = session_ref.collection("chunks").document(chunk_id)
                
                chunk_ref.set({
                    "timestamp": firestore.SERVER_TIMESTAMP,
                    "samples": batch_samples
                })
                
                chunk_index += 1
                batch_samples = []

        except KeyboardInterrupt:
            print("\n👋 Server shutting down...")
            try:
                if batch_samples:
                    nav_state = integration.integrate_batch(batch_samples, nav_state, MAG_OFFSET_X, MAG_OFFSET_Y)
                    chunk_id = f"chunk_{chunk_index:05d}"
                    session_ref.collection("chunks").document(chunk_id).set({
                        "timestamp": firestore.SERVER_TIMESTAMP,
                        "samples": batch_samples
                    })
                session_ref.update({"status": "FINISHED"})
                print("📡 Session marked as FINISHED.")
            except Exception as e:
                print(f"⚠️ Shutdown update failed: {e}")
            break
        except Exception as e:
            print(f"⚠️ Loop Error: {e}")

if __name__ == "__main__":
    main()