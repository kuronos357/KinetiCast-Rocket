# config.py
UDP_IP = "0.0.0.0"      # 全インターフェースで待ち受け
UDP_PORT = 9870
PACKET_FORMAT = "<Iffffff"  # timestamp_ms(I) + ax,ay,az,gx,gy,gz(f×6)
FIREBASE_KEY_PATH = "seacrets/kineticast-firebase-adminsdk.json"  # Firebaseサービスアカウントキーのパス
BATCH_SIZE = 100  # 1秒あたりのIMUサンプル数