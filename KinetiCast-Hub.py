import socket
import struct
import time
import csv

# ---- 設定 ----
UDP_IP = "0.0.0.0"      # 全インターフェースで待ち受け
UDP_PORT = 9870          # マイコン側のUDP_PORTと一致させる
OUTPUT_CSV = "udp_receive_log.csv"

# パケット構造体フォーマット（マイコン側のImuPacketと一致させる）
# uint32_t timestamp_ms, float ax,ay,az, float gx,gy,gz
# I = uint32(4byte), f = float(4byte) x6 = 合計28byte
PACKET_FORMAT = "<Iffffff"
PACKET_SIZE = struct.calcsize(PACKET_FORMAT)

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    print(f"Listening on 0.0.0.0:{UDP_PORT} (packet size expected: {PACKET_SIZE} bytes)")
    print("Ctrl+C で停止します。")

    rows = []
    count = 0
    last_recv_time = None

    try:
        while True:
            data, addr = sock.recvfrom(1024)
            recv_time = time.perf_counter()  # 高精度タイマー

            if len(data) != PACKET_SIZE:
                print(f"[WARN] 予期しないパケットサイズ: {len(data)} bytes (期待値: {PACKET_SIZE})")
                continue

            ts_ms, ax, ay, az, gx, gy, gz = struct.unpack(PACKET_FORMAT, data)

            interval_ms = None
            if last_recv_time is not None:
                interval_ms = (recv_time - last_recv_time) * 1000.0
            last_recv_time = recv_time

            rows.append([count, ts_ms, ax, ay, az, gx, gy, gz, interval_ms])
            count += 1

            # 100件ごとに直近の受信間隔を表示
            if count % 100 == 0:
                recent_intervals = [r[8] for r in rows[-100:] if r[8] is not None]
                avg_interval = sum(recent_intervals) / len(recent_intervals) if recent_intervals else 0
                print(f"[{count}] 直近100件の平均受信間隔: {avg_interval:.2f} ms "
                      f"(対応周波数: {1000/avg_interval:.1f} Hz)" if avg_interval > 0 else "")

    except KeyboardInterrupt:
        print("\n停止しました。CSVに保存します...")

    finally:
        sock.close()
        with open(OUTPUT_CSV, "w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["seq", "device_timestamp_ms", "ax", "ay", "az",
                              "gx", "gy", "gz", "recv_interval_ms"])
            writer.writerows(rows)
        print(f"保存完了: {OUTPUT_CSV} ({len(rows)} 件)")

        # 全体の統計を表示
        intervals = [r[8] for r in rows if r[8] is not None]
        if intervals:
            avg = sum(intervals) / len(intervals)
            print(f"\n=== 統計 ===")
            print(f"総受信数: {len(rows)}")
            print(f"平均受信間隔: {avg:.2f} ms (対応周波数: {1000/avg:.1f} Hz)")
            print(f"最小間隔: {min(intervals):.2f} ms")
            print(f"最大間隔: {max(intervals):.2f} ms")


if __name__ == "__main__":
    main()