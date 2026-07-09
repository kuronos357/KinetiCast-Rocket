import time
import base64
import firebase_admin
from firebase_admin import credentials, firestore

_db = None

def init_firebase(key_path):
    global _db
    try:
        cred = credentials.Certificate(key_path)
        firebase_admin.initialize_app(cred)
        _db = firestore.client()
        print("🟢 Firebase Admin SDK Initialized Successfully.")
        return _db
    except Exception as e:
        print(f"❌ Firebase Init Failed: {e}")
        return None

def create_session():
    if not _db: return None
    session_id = f"launch_session_{int(time.time())}"
    doc_ref = _db.collection("sessions").document(session_id)
    doc_ref.set({
        "createdAt": firestore.SERVER_TIMESTAMP,
        "imageUrl": None,
        "status": "LIVE"
    })
    print(f"🚀 New Active Rocket Session Created: {session_id}")
    return session_id

def update_live_image(session_id, jpeg_bytes):
    if not _db or not session_id: return
    try:
        base64_encoded = base64.b64encode(jpeg_bytes).decode('utf-8')
        data_url = f"data:image/jpeg;base64,{base64_encoded}"
        doc_ref = _db.collection("sessions").document(session_id)
        doc_ref.update({
            "imageUrl": data_url,
            "updatedAt": firestore.SERVER_TIMESTAMP
        })
    except Exception as e:
        print(f"⚠️ Firebase Update Failed: {e}")