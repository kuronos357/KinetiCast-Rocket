import socket
import struct
import math
import time
import datetime
import firebase_admin
from firebase_admin import credentials, firestore

# ==========================================
# 1. ネットワーク & Firebase 設定
# ==========================================
UDP_IP = "0.0.0.0"
UDP_PORT = 9870

# Firebaseからダウンロードした秘密鍵のファイル名・パス
CRED_PATH = "0_Project\\KinetiCast-Rocket\\KinetiCast-Hub\\secrets\\kineticast-firebase-adminsdk.json" 

try:
    cred = credentials.Certificate(CRED_PATH)
    firebase_admin.initialize_app(cred)
    db = firestore.client()
    print("🟢 Firebase Admin SDK initialized successfully. Production Mode Active.")
except Exception as e:
    print(f"🔴 Firebase initialization failed: {e}")
    print("🔴 secrets/kineticast-firebase-adminsdk.json が正しいパスにあるか確認してください。")
    exit(1)

# ==========================================
# 2. テレメトリ解析・計算設定
# ==========================================
PACKET_FORMAT = '<Ifffffffff'
PACKET_SIZE = 40

G_TO_MS2 = 9.80665
vx, vy, vz = 0.0, 0.0, 0.0
px, py, pz = 0.0, 0.0, 0.0
last_time_s = None

BATCH_SIZE_LIMIT = 100 # 100サンプル（約1秒分）ごとに1つの子ドキュメントとしてプッシュ

MAG_OFFSET_X = 130.0
MAG_OFFSET_Y = -350.0
MAG_OFFSET_Z = 120.0

def process_navigation(timestamp_ms, ax, ay, az, gx, gy, gz, mx, my, mz):
    """加速度から速度・位置を計算し、地磁気から絶対方位を計算する"""
    global vx, vy, vz, px, py, pz, last_time_s
    
    current_time_s = timestamp_ms / 1000.0
    
    if last_time_s is None:
        last_time_s = current_time_s
        return 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0
        
    dt = current_time_s - last_time_s
    last_time_s = current_time_s
    
    if dt <= 0 or dt > 0.5: 
        dt = 0.01 

    ax_ms2 = ax * G_TO_MS2
    ay_ms2 = ay * G_TO_MS2
    az_ms2 = (az - 1.0) * G_TO_MS2 
    
    vx += ax_ms2 * dt
    vy += ay_ms2 * dt
    vz += az_ms2 * dt
    
    px += vx * dt
    py += vy * dt
    pz += vz * dt
    
    calibrated_mx = mx - MAG_OFFSET_X
    calibrated_my = my - MAG_OFFSET_Y
    
    heading_rad = math.atan2(calibrated_my, calibrated_mx)
    heading_deg = math.degrees(heading_rad)
    if heading_deg < 0:
        heading_deg += 360.0
        
    return vx, vy, vz, px, py, pz, heading_deg

def save_chunk_to_firebase(session_id, chunk_id, samples):
    """最新セッションの子コレクション(chunks)としてデータをFirestoreへ保存"""
    try:
        # パス構造: sessions/{session_id}/chunks/{chunk_id}
        chunk_ref = db.collection("sessions").document(session_id).collection("chunks").document(chunk_id)
        
        payload = {
            "timestamp": firestore.SERVER_TIMESTAMP,
            "samples": samples
        }
        
        chunk_ref.set(payload)
        print(f"🚀 [Firestore] Pushed {chunk_id} to {session_id} ({len(samples)} samples written)")
        
    except Exception as e:
        print(f"🔴 Firestore Write Error: {e}")

# ==========================================
# 3. UDP 受信メインループ
# ==========================================
def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024 * 1024)
    
    print(f"📦 KinetiCast 9-Axis Backend Active. Listening on {UDP_IP}:{UDP_PORT} ...")

    # 🚀 1. main.py 起動時に、日時ベースの一意なセッションIDを生成して親ドキュメントを生成
    session_id = datetime.datetime.now().strftime("session_%Y%m%d_%H%M%S")
    session_ref = db.collection("sessions").document(session_id)
    
    print(f"📡 New Flight Session Created: {session_id}")
    session_ref.set({
        "createdAt": firestore.SERVER_TIMESTAMP,
        "status": "LIVE",
        "imageUrl": None
    })

    batch_samples = []
    chunk_index = 0

    while True:
        try:
            data, addr = sock.recvfrom(1024)
            
            if len(data) != PACKET_SIZE:
                continue
                
            parsed = struct.unpack(PACKET_FORMAT, data)
            t_ms, ax, ay, az, gx, gy, gz, mx, my, mz = parsed
            
            vx_calc, vy_calc, vz_calc, px_calc, py_calc, pz_calc, heading_calc = process_navigation(
                t_ms, ax, ay, az, gx, gy, gz, mx, my, mz
            )
            
            # 💡 Viewer.tsx の TelemetrySample インターフェースのキー名に完全に一致させる (フラット構造)
            sample_node = {
                "t": t_ms,
                "ax": ax, "ay": ay, "az": az,
                "gx": gx, "gy": gy, "gz": gz,
                "mx": mx, "my": my, "mz": mz,
                "vx": vx_calc, "vy": vy_calc, "vz": vz_calc,
                "px": px_calc, "py": py_calc, "pz": pz_calc,
                "heading": heading_calc  # Python側の高度な校正済み方位を直接渡す
            }
            
            batch_samples.append(sample_node)
            
            # 100サンプル（約1秒分）貯まったら子チャンクとしてFirestoreへ送信
            if len(batch_samples) >= BATCH_SIZE_LIMIT:
                chunk_id = f"chunk_{chunk_index:05d}" # chunk_00000, chunk_00001... のように連番化
                save_chunk_to_firebase(session_id, chunk_id, batch_samples)
                
                chunk_index += 1
                batch_samples = []

        except KeyboardInterrupt:
            print("\n👋 Server shutting down...")
            # 終了時にセッションのステータスを完了にする
            try:
                if batch_samples:
                    chunk_id = f"chunk_{chunk_index:05d}"
                    save_chunk_to_firebase(session_id, chunk_id, batch_samples)
                session_ref.update({"status": "FINISHED"})
                print("📡 Session marked as FINISHED.")
            except Exception as e:
                print(f"⚠️ Shutdown update failed: {e}")
            break
        except Exception as e:
            print(f"⚠️ Loop Error: {e}")

if __name__ == "__main__":
    main()