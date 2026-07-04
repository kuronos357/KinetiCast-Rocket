# config.py
UDP_IP = "0.0.0.0"      # 全インターフェースで待ち受け
UDP_PORT = 9870
PACKET_FORMAT = "<Iffffff"  # timestamp_ms(I) + ax,ay,az,gx,gy,gz(f×6)