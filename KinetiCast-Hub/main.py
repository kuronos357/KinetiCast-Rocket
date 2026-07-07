import socket
import struct
from config import UDP_IP, UDP_PORT, PACKET_FORMAT, BATCH_SIZE
from firebase_client import init_firebase, create_flight, upload_batch, mark_flight_completed
from integration import integrate_batch


def main():
    init_firebase()
    flight_id = create_flight()
    print(f"Flight started: {flight_id}")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    print(f"Listening on {UDP_IP}:{UDP_PORT} ...")

    packet_size = struct.calcsize(PACKET_FORMAT)

    batch_samples = []
    batch_index = 0
    batch_t_start = None

    state = {
        "gravity_ref": None,
        "calib_buffer": [],
        "quat": (1.0, 0.0, 0.0, 0.0),
        "vx": 0.0, "vy": 0.0, "vz": 0.0,
        "px": 0.0, "py": 0.0, "pz": 0.0,
        "last_t": None
    }

    try:
        while True:
            data, addr = sock.recvfrom(1024)

            if len(data) != packet_size:
                print(f"[WARN] Unexpected packet size: {len(data)} bytes from {addr}")
                continue

            timestamp_ms, ax, ay, az, gx, gy, gz = struct.unpack(PACKET_FORMAT, data)

            if batch_t_start is None:
                batch_t_start = timestamp_ms

            batch_samples.append({
                "t": timestamp_ms,
                "ax": ax, "ay": ay, "az": az,
                "gx": gx, "gy": gy, "gz": gz
            })

            if len(batch_samples) >= BATCH_SIZE:
                state = integrate_batch(batch_samples, state)

                upload_batch(flight_id, batch_index, batch_t_start, timestamp_ms, batch_samples)

                # print文もクォータニオン表示に変更
                print(f"Batch {batch_index} uploaded "
                    f"(quat=({state['quat'][0]:.2f}, {state['quat'][1]:.2f}, "
                    f"{state['quat'][2]:.2f}, {state['quat'][3]:.2f}), "
                    f"pos=({state['px']:.2f}, {state['py']:.2f}, {state['pz']:.2f})m)")

                batch_index += 1
                batch_samples = []
                batch_t_start = None

    except KeyboardInterrupt:
        print("\nStopped by user.")
        mark_flight_completed(flight_id)
        print(f"Flight {flight_id} marked as completed.")
    finally:
        sock.close()


if __name__ == "__main__":
    main()