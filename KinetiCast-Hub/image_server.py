import os
import time
import struct
import serial
import firebase_admin
from firebase_admin import credentials, firestore, storage

# ==========================================
# 1. Firebase Admin SDK の初期化 (既存の設定を維持)
# ==========================================
CRED_PATH = "0_Project\\KinetiCast-Rocket\\KinetiCast-Hub\\secrets\\kineticast-firebase-adminsdk.json"
BUCKET_NAME = "あなたのプロジェクトID.firebasestorage.app"  # 💡お使いのバケット名に書き換えてください

try:
    cred = credentials.Certificate(CRED_PATH)
    firebase_admin.initialize_app(cred, {
        'storageBucket': BUCKET_NAME
    })
    db = firestore.client()
    bucket = storage.bucket()
    print("🟢 Firebase Admin SDK (Storage & Firestore) initialized.")
except Exception as e:
    print(f"🔴 Firebase initialization failed: {e}")
    exit(1)

# ==========================================
# 2. シリアル通信設定 (環境に合わせて調整)
# ==========================================
# 💡 S3をPCに接続した際のCOMポート名に書き換えてください（例: 'COM3', '/dev/ttyACM0' など）
SERIAL_PORT = 'COM3' 
BAUD_RATE = 115200 

# フレームの連番用カウンター
frame_index = 0

def process_image(image_data):
    global frame_index
    try:
        # ------------------------------------------
        # 🔍 A. 現在アクティブ（RUNNING）なセッションを検索
        # ------------------------------------------
        sessions_ref = db.collection("sessions")
        active_sessions = (
            sessions_ref.where("status", "==", "RUNNING")
            .limit(1)
            .get()
        )

        if not active_sessions:
            print("⚠️ [Warning] No RUNNING session found. Skipping Firebase sync.")
            return

        session_doc = active_sessions[0]
        session_id = session_doc.id

        # ------------------------------------------
        # 📤 B. Firebase Storage へアップロード
        # ------------------------------------------
        blob_path = f"sessions/{session_id}/frames/frame_{frame_index:05d}.jpg"
        blob = bucket.blob(blob_path)
        
        # バイトデータを直接Storageに転送
        blob.upload_from_string(image_data, content_type='image/jpeg')
        
        # 公開ダウンロードURLを発行
        blob.make_public()
        public_url = blob.public_url

        # ------------------------------------------
        # 📡 C. Firestore の親ドキュメントの URL を更新
        # ------------------------------------------
        sessions_ref.document(session_id).update({
            "imageUrl": public_url,
            "lastImageUpdatedAt": firestore.SERVER_TIMESTAMP
        })

        print(f"🚀 [Firebase Sync] Session: {session_id} -> Synced Frame {frame_index:05d} ({len(image_data)//1024} KB)")
        print(f"🔗 URL: {public_url}")

        frame_index += 1

    except Exception as e:
        print(f"🔴 [Error] Failed to process/upload image: {e}")

def main():
    print(f"🔌 Connecting to M5Stack S3 on {SERIAL_PORT}...")
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    except Exception as e:
        print(f"🔴 Failed to open serial port {SERIAL_PORT}: {e}")
        print("💡 接続されているCOMポート番号が正しいか確認してください。")
        return

    print("🟢 Serial connection established. Waiting for rocket camera data via ESP-NOW...")
    
    buffer = b""
    
    while True:
        try:
            # シリアルバッファからデータを一括読み込み
            if ser.in_waiting > 0:
                buffer += ser.read(ser.in_waiting)
            else:
                time.sleep(0.001) # CPU負荷下げ
                continue
            
            # マーカー "IMG:" を探してパース
            while b"IMG:" in buffer:
                idx = buffer.index(b"IMG:")
                buffer = buffer[idx:] # マーカーより前のゴミデータをカット
                
                if len(buffer) < 8:
                    break # サイズ情報(4バイト)が溜まるまで次の受信を待つ
                
                # 4バイトのデータサイズ(Little Endian)を取得
                img_size = struct.unpack("<I", buffer[4:8])[0]
                
                if len(buffer) < 8 + img_size:
                    break # 画像本体がすべて溜まるまで次の受信を待つ
                
                # 画像バイナリを切り出し
                img_data = buffer[8:8+img_size]
                buffer = buffer[8+img_size:] # 読み終わった分をバッファから削除
                
                # Firebase処理へ引き渡し
                process_image(img_data)
                
        except KeyboardInterrupt:
            print("\n👋 Server shutting down...")
            ser.close()
            break
        except Exception as e:
            print(f"⚠️ Loop Error: {e}")
            time.sleep(1)

if __name__ == "__main__":
    main()