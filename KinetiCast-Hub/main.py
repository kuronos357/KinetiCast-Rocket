# main.py
import socket
import struct
from config import UDP_IP, UDP_PORT, PACKET_FORMAT

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    print(f"Listening on {UDP_IP}:{UDP_PORT} ...")

    packet_size = struct.calcsize(PACKET_FORMAT)

    try:
        while True:
            data, addr = sock.recvfrom(1024)

            if len(data) != packet_size:
                print(f"[WARN] Unexpected packet size: {len(data)} bytes from {addr}")
                continue

            timestamp_ms, ax, ay, az, gx, gy, gz = struct.unpack(PACKET_FORMAT, data)
            print(f"t={timestamp_ms:>8} ms | "
                  f"accel=({ax:+.4f}, {ay:+.4f}, {az:+.4f}) | "
                  f"gyro=({gx:+.4f}, {gy:+.4f}, {gz:+.4f})")

    except KeyboardInterrupt:
        print("\nStopped by user.")
    finally:
        sock.close()

if __name__ == "__main__":
    main()