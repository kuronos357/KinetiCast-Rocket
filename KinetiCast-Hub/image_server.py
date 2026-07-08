import os
import time
from flask import Flask, request
import firebase_admin
from firebase_admin import credentials, firestore, storage

app = Flask(__name__)

# ==========================================
# 1. Firebase Admin SDK の初期化 (Storageバケット対応)
# ==========================================
# main.py と同じ秘密鍵のパスを指定してください
CRED_PATH = "0_Project\\KinetiCast-Rocket\\KinetiCast-Hub\\secrets\\kineticast-firebase-adminsdk.json"
# 💡 あなたのFirebaseプロジェクトのStorageバケット名（例: my-rocket-project.firebasestorage.app）
# ※ FirebaseコンソールのStorage画面上部に記載されている「gs://〜」の文字列です
BUCKET_NAME = "あなたのプロジェクトID.firebasestorage.app" 

try:
    cred = credentials.Certificate(CRED_PATH)
    firebase_admin.initialize_app(cred, {
        'storageBucket': BUCKET_NAME
    })
    db = firestore.client()
    bucket = storage.bucket()
    print("🟢 Firebase Admin SDK (Storage & Firestore) initialized inside Image Server.")
except Exception as e:
    print(f"🔴 Firebase initialization failed: {e}")
    exit(1)

# フレームの連番用カウンター
frame_index = 0

# ==========================================
# 2. 画像受信 ＆ Firebase自動同期エンドポイント
# ==========================================
@app.route("/upload_image", methods=["POST"])
def upload_image():
    global frame_index
    try:
        image_data = request.data
        if not image_data:
            return "No data received", 400

        filename = request.headers.get("X-Filename", f"frame_{int(time.time())}.jpg")
        print(f"📸 [RECEIVED] Frame from onboard cam ({len(image_data)} bytes)")

        # ------------------------------------------
        # 🔍 A. 現在稼働中 (LIVE) のセッションIDを検索
        # ------------------------------------------
        sessions_ref = db.collection("sessions")
        active_sessions = (
            sessions_ref.where("status", "==", "LIVE")
            .order_by("createdAt", direction=firestore.Query.DESCENDING)
            .limit(1)
            .get()
        )

        if not active_sessions:
            print("⚠️ [Firebase] No active LIVE session found. Skipping Firebase sync.")
            return "OK (No Active Session)", 200

        session_doc = active_sessions[0]
        session_id = session_doc.id

        # ------------------------------------------
        # 📤 B. Firebase Storage へアップロード
        # ------------------------------------------
        # セッションごとにディレクトリを分けて格納
        blob_path = f"sessions/{session_id}/frames/frame_{frame_index:05d}.jpg"
        blob = bucket.blob(blob_path)
        
        # バイトデータを直接Storageに転送 (ブラウザ表示用に Content-Type を指定)
        blob.upload_from_string(image_data, content_type='image/jpeg')
        
        # 誰でもアクセスできる公開ダウンロードURLを発行
        blob.make_public()
        public_url = blob.public_url

        # ------------------------------------------
        # 📡 C. Firestore の親ドキュメントの URL を更新
        # ------------------------------------------
        sessions_ref.document(session_id).update({
            "imageUrl": public_url,
            "lastImageUpdatedAt": firestore.SERVER_TIMESTAMP
        })

        print(f"🚀 [Firebase Sync] Session: {session_id} -> Synced Frame {frame_index:05d}")
        print(f"🔗 URL: {public_url}")

        frame_index += 1
        return "OK", 200

    except Exception as e:
        print(f"❌ [SERVER ERROR] Failed during image processing: {e}")
        return f"Error: {e}", 500

if __name__ == "__main__":
    # ローカルネットワーク内のカメラデバイス(ESP32-CAMなど)から叩けるよう 0.0.0.0 で待機
    app.run(host="0.0.0.0", port=5000)