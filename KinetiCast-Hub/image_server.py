import os
import struct
import sys
import time
import cv2
import numpy as np
import serial
from serial.tools import list_ports

# =========================================================
# ⚙️ 設定
# =========================================================
BAUDRATE = 115200
SAVE_DIR = "captured_images"

# 保存用ディレクトリの作成
if not os.path.exists(SAVE_DIR):
    os.makedirs(SAVE_DIR)

def select_com_port():
    """PCに接続されているCOMポートを自動検出し、M5Stackっぽいものを最優先で自動選択する"""
    ports = list(list_ports.comports())
    if not ports:
        print("❌ エラー: 利用可能なCOMポートが見つかりません。")
        sys.exit(1)

    print("\n--- 🔍 利用可能なシリアルポート一覧 ---")
    m5_ports = []
    for i, p in enumerate(ports):
        is_m5 = "USB シリアル" in p.description or "m5stack" in p.description.lower() or "ch340" in p.description.lower() or "cp210x" in p.description.lower()
        marker = "⭐ [本命候補]" if is_m5 else ""
        print(f" [{i}] {p.device} - {p.description} {marker}")
        if is_m5:
            m5_ports.append(p.device)

    if m5_ports:
        chosen_port = m5_ports[0]
        print(f"\n💡 M5Stack候補を自動検出したため、【{chosen_port}】 を選択しました。")
        return chosen_port

    if len(ports) == 1:
        print(f"💡 1つのポートのみ検出されたため、{ports[0].device} を自動選択しました。")
        return ports[0].device

    print(f"\n⚠️ 自動判別できなかったため、リストの最初にある 【{ports[0].device}】 で自動起動します。")
    return ports[0].device

def main():
    com_port = select_com_port()

    print(f"\n📡 {com_port} をボーレート {BAUDRATE} でオープンします...")
    try:
        ser = serial.Serial(
            com_port,
            BAUDRATE,
            timeout=1,
            dsrdtr=True,
            rtscts=False,
            xonxoff=False,
        )
        ser.set_buffer_size(rx_size=1024 * 1024, tx_size=1024)
    except Exception as e:
        print(f"❌ ポートのオープンに失敗しました: {e}")
        sys.exit(1)

    print("🟢 サーバー起動完了。ロケットからの画像待機中...")
    print("   ※ 終了するには画像ウィンドウを選択した状態で 'q' キーを押してください。\n")

    cv2.namedWindow("🚀 Rocket Live View", cv2.WINDOW_NORMAL)
    img_count = 0

    try:
        while True:
            # 1. 1バイトずつ確実にスキャンして "IMG:" マーカーを探す（文字化け回避）
            sync_buffer = b""
            while True:
                char = ser.read(1)
                if not char:
                    break
                sync_buffer += char
                if len(sync_buffer) > 4:
                    sync_buffer = sync_buffer[1:]
                if sync_buffer == b"IMG:":
                    break
            
            if sync_buffer != b"IMG:":
                continue # タイムアウト等で抜けた場合は最初からやり直し

            # 2. 画像サイズ(4バイト)を取得
            size_bytes = ser.read(4)
            if len(size_bytes) < 4:
                continue

            (img_size,) = struct.unpack("<I", size_bytes)

            if img_size == 0 or img_size > 5 * 1024 * 1024:
                continue

            # コンソールへの経過表示（不要ならこのprintも消してOKです）
            print(f"📥 受信中... サイズ: {img_size:,} bytes", end="", flush=True)

            # 3. 指定サイズ分のバイナリデータを取得
            img_data = b""
            start_time = time.time()
            while len(img_data) < img_size:
                if time.time() - start_time > 3.0:
                    break
                chunk = ser.read(min(img_size - len(img_data), 4096))
                if not chunk:
                    break
                img_data += chunk

            if len(img_data) < img_size:
                print(" -> ❌ タイムアウト")
                continue

            print(" -> 🟢 完了！")

            # 4. デコードと表示
            nparr = np.frombuffer(img_data, np.uint8)
            img = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

            if img is None:
                continue

            img_count += 1
            filename = os.path.join(
                SAVE_DIR, f"rocket_{img_count:05d}_{int(time.time())}.jpg"
            )
            cv2.imwrite(filename, img)

            cv2.putText(
                img,
                f"REC: {img_count}",
                (10, 30),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.8,
                (0, 255, 0),
                2,
            )
            cv2.imshow("🚀 Rocket Live View", img)

            if cv2.waitKey(1) & 0xFF == ord("q"):
                print("ユーザー要求により終了します。")
                break

    except KeyboardInterrupt:
        print("\nサーバーを停止します。")
    finally:
        ser.close()
        cv2.destroyAllWindows()
        print("🔌 終了しました。")

if __name__ == "__main__":
    main()