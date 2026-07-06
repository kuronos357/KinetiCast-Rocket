import os

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
FIREBASE_KEY_PATH = os.path.join(BASE_DIR, "secrets", "kineticast-firebase-adminsdk.json")
UDP_IP = "0.0.0.0"      # 全インターフェースで待ち受け
UDP_PORT = 9870
PACKET_FORMAT = "<Iffffff"  # timestamp_ms(I) + ax,ay,az,gx,gy,gz(f×6)
BATCH_SIZE = 100  # 1秒あたりのIMUサンプル数