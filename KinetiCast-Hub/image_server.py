from flask import Flask, request
import os
import time

app = Flask(__name__)
SAVE_DIR = "received_images"
os.makedirs(SAVE_DIR, exist_ok=True)

@app.route("/upload_image", methods=["POST"])
def upload_image():
    filename = request.headers.get("X-Filename", f"unknown_{int(time.time())}.jpg")
    filepath = os.path.join(SAVE_DIR, filename.lstrip("/"))

    with open(filepath, "wb") as f:
        f.write(request.data)

    print(f"[RECEIVED] {filename} ({len(request.data)} bytes)")
    return "OK", 200

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)