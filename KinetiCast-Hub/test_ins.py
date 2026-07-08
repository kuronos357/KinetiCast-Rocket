# test_ins.py
import pandas as pd
import integration

# SDカードから回収したCSVファイルを読み込む (ファイル名は適宜合わせてください)
# テスト用に、手でM5Atomを上に持ち上げて下ろした時のログを指定します
df = pd.read_csv(r"I:\flight_log_3.csv")

# 航法状態の初期化
nav_state = {
    "quat": [0.0, 0.0, 0.0, 1.0],
    "vx": 0.0, "vy": 0.0, "vz": 0.0,
    "px": 0.0, "py": 0.0, "pz": 0.0,
    "last_t": None
}

# CSVの行を辞書のリストに変換
samples = []
for _, row in df.iterrows():
    samples.append({
        "t": row["timestamp"],
        "ax": row["ax"], "ay": row["ay"], "az": row["az"],
        "gx": row["gx"], "gy": row["gy"], "gz": row["gz"]
    })

# 航法計算の実行
# 本番のmain.pyと同じように、データをまとめて一気に計算させます
nav_state = integration.integrate_batch(samples, nav_state)

# 計算された「最後の位置（着地時）」を表示
print("\n=== INS Simulation Result ===")
print(f"Final Position X: {nav_state['px']:.2f} m")
print(f"Final Position Y: {nav_state['vy']:.2f} m")
print(f"Final Position Z (高度): {nav_state['pz']:.2f} m 👈 ここが0m付近なら大成功！")