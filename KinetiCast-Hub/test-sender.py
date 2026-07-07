# test_sender.py (フロントエンドテスト用ダミー送信機)
import time
import math
from firebase_client import init_firebase, create_flight, upload_batch

def generate_dummy_flight():
    init_firebase()
    flight_id = create_flight()
    print(f"[TEST] Dummy Flight Started: {flight_id}")
    print("これを出先のフロントエンド画面に入力するか、FirestoreでこのIDを確認してください。")

    batch_index = 0
    t_ms = 0
    
    # 疑似的なロケットの上昇軌道データを作る（10秒間送る）
    for second in range(10):
        samples = []
        t_start = t_ms
        
        for i in range(100): # 1秒分(100Hz)
            t_ms += 10
            dt = t_ms / 1000.0
            
            # 適当に変化するダミーの位置とクォータニオン
            height = 0.5 * 9.8 * (dt**2)  # 等加速度直線運動っぽいZ軸位置
            
            samples.append({
                "t": t_ms,
                "ax": 0.0, "ay": 0.0, "az": 1.2, # 加速度
                "gx": 0.0, "gy": 5.0 * math.sin(dt), "gz": 0.0, # 角速度
                "vx": 0.0, "vy": 0.0, "vz": 9.8 * dt,
                "px": 0.0, "py": 0.0, "pz": height, # 位置
                "qw": math.cos(dt*0.1), "qx": 0.0, "qy": math.sin(dt*0.1), "qz": 0.0 # クォータニオン
            })
            
        t_end = t_ms
        upload_batch(flight_id, batch_index, t_start, t_end, samples)
        print(f"[TEST] Uploaded batch_{batch_index:04d} (Height: {height:.2f}m)")
        batch_index += 1
        
        time.sleep(1.0) # 1秒待って次のバッチを送る（本番の挙動を再現）

if __name__ == "__main__":
    generate_dummy_flight()