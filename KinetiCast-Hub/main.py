import socket
import struct
import time
import datetime
import os
import sys
import firebase_admin
from firebase_admin import credentials, firestore
import integration

# ---- 設定 ----
UDP_IP = "0.0.0.0"
UDP_PORT = 9870
CRED_PATH = "0_Project\\KinetiCast-Rocket\\KinetiCast-Hub\\secrets\\kineticast-firebase-adminsdk.json"

try:
    cred = credentials.Certificate(CRED_PATH)
    firebase_admin.initialize_app(cred)
    db = firestore.client()
    print("🟢 Firebase Admin SDK initialized successfully.")
except Exception as e:
    print(f"🔴 Firebase initialization failed: {e}")
    exit(1)

# パケット解析設定 (28バイトのパケット: uint32 + float*6)
PACKET_FORMAT = '<Iffffff'
PACKET_SIZE = 28
BATCH_SIZE_LIMIT = 50

nav_state = {
    "quat": [0.0, 0.0, 0.0, 1.0],
    "vx": 0.0, "vy": 0.0, "vz": 0.0,
    "px": 0.0, "py": 0.0, "pz": 0.0,
    "last_t": None,
    "calib_buffer": []
}

# Firebaseセッションの作成
session_id = f"session_{datetime.datetime.now().strftime('%Y%m%d_%H%M%S')}"
session_ref = db.collection("sessions").document(session_id)
session_ref.set({
    "launched_at": firestore.SERVER_TIMESTAMP,
    "status": "RUNNING"
})
print(f"🚀 Telemetry session started: {session_id}")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))
print(f"📡 UDP Receiver listening on port {UDP_PORT}...")

batch_samples = []
chunk_index = 0

try:
    while True:
        data, addr = sock.recvfrom(1024)
        if len(data) < PACKET_SIZE:
            continue

        # 受信データのパース
        parsed = struct.unpack(PACKET_FORMAT, data[:PACKET_SIZE])
        sample = {
            "t": parsed[0],
            "ax": parsed[1], "ay": parsed[2], "az": parsed[3],
            "gx": parsed[4], "gy": parsed[5], "gz": parsed[6]
        }
        batch_samples.append(sample)

        # バッチ処理（50パケット = 0.5秒ごとに航法計算とクラウド送信）
        if len(batch_samples) >= BATCH_SIZE_LIMIT:
            nav_state = integration.integrate_batch(batch_samples, nav_state)
            
            # コンソール画面のリアルタイム上書き表示
            sys.stdout.write(
                f"\r🤖 [INS] Alt(Z): {nav_state['pz']:.2f} m | Vel Z: {nav_state['vz']:.2f} m/s | Pos X: {nav_state['px']:.2f} m, Y: {nav_state['py']:.2f} m"
            )
            sys.stdout.flush()

            # Firebase Firestoreへパケットを保存
            chunk_id = f"chunk_{chunk_index:05d}"
            chunk_ref = session_ref.collection("chunks").document(chunk_id)
            chunk_ref.set({
                "timestamp": firestore.SERVER_TIMESTAMP,
                "samples": batch_samples
            })
            
            chunk_index += 1
            batch_samples = []

except KeyboardInterrupt:
    print("\n👋 Server shutting down gracefully...")
    if batch_samples:
        nav_state = integration.integrate_batch(batch_samples, nav_state)
        chunk_id = f"chunk_{chunk_index:05d}"
        session_ref.collection("chunks").document(chunk_id).set({
            "timestamp": firestore.SERVER_TIMESTAMP,
            "samples": batch_samples
        })
    session_ref.update({"status": "FINISHED"})
    print("📡 Session marked as FINISHED.")
except Exception as e:
    print(f"\n⚠️ Loop Error: {e}")
finally:
    sock.close()