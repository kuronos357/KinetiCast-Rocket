import time
import firebase_admin
from firebase_admin import credentials, firestore
from config import FIREBASE_KEY_PATH

_db = None

def init_firebase():
    global _db
    cred = credentials.Certificate(FIREBASE_KEY_PATH)
    firebase_admin.initialize_app(cred)
    _db = firestore.client()
    return _db

def create_flight():
    flight_id = str(int(time.time()))
    doc_ref = _db.collection("flights").document(flight_id)
    doc_ref.set({
        "created_at": firestore.SERVER_TIMESTAMP,
        "status": "in_progress"
    })
    return flight_id

def upload_batch(flight_id, batch_index, t_start, t_end, samples):
    batch_ref = (
        _db.collection("flights")
           .document(flight_id)
           .collection("telemetry")
           .document(f"batch_{batch_index:04d}")
    )
    batch_ref.set({
        "t_start": t_start,
        "t_end": t_end,
        "samples": samples
    })

def mark_flight_completed(flight_id):
    _db.collection("flights").document(flight_id).update({
        "status": "completed"
    })