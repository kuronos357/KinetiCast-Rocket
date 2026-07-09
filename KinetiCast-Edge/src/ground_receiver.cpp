#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h> // 💡 チャンネル設定の内部APIを使うために追加

#define CHUNK_SIZE 230
#pragma pack(push, 1)
struct Packet {
  uint16_t image_id;
  uint16_t total_chunks;
  uint16_t chunk_idx;
  uint8_t data_len;
  uint8_t payload[CHUNK_SIZE];
};
#pragma pack(pop)

// 画像格納用バッファ（400KBあればUXGAでも余裕）
#define MAX_IMG_SIZE (400 * 1024)
uint8_t* imgBuffer = nullptr;

uint16_t currentImageId = 0xFFFF;
uint16_t chunksReceived = 0;
uint32_t maxOffsetDetected = 0;

// PCへシリアルデータを送信する関数
void sendToPC(uint32_t dataLen) {
  if (dataLen == 0) return;
  // マーカー"IMG:"(4文字) + データサイズ(4バイト) + JPEG中身
  Serial.write("IMG:");
  Serial.write((uint8_t*)&dataLen, 4);
  Serial.write(imgBuffer, dataLen);
  Serial.flush();
}

// ESP-NOW 受信コールバック関数
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  if (len < 7 || imgBuffer == nullptr) return;

  Packet pkt;
  memcpy(&pkt, incomingData, len);

  // 新しい画像IDが飛んできたら、前の画像を（不完全でも）PCに吐き出してリセット
  if (pkt.image_id != currentImageId) {
    if (currentImageId != 0xFFFF && chunksReceived > 0) {
      sendToPC(maxOffsetDetected);
    }
    currentImageId = pkt.image_id;
    chunksReceived = 0;
    maxOffsetDetected = 0;
  }

  uint32_t offset = pkt.chunk_idx * CHUNK_SIZE;
  if (offset + pkt.data_len < MAX_IMG_SIZE) {
    memcpy(imgBuffer + offset, pkt.payload, pkt.data_len);
    chunksReceived++;
    
    uint32_t endPoint = offset + pkt.data_len;
    if (endPoint > maxOffsetDetected) {
      maxOffsetDetected = endPoint;
    }

    // すべてのパケットが揃ったら即座にPCへ送信
    if (chunksReceived >= pkt.total_chunks) {
      sendToPC(maxOffsetDetected);
      currentImageId = 0xFFFF; // 次のフレーム待ちにするため初期化
      chunksReceived = 0;
    }
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  // 400KBの大きなバッファを確保（S3世代なら内蔵RAMで余裕）
  imgBuffer = (uint8_t*)malloc(MAX_IMG_SIZE);
  if (imgBuffer == nullptr) {
    Serial.println("❌ Failed to allocate image buffer!");
    while(1) delay(10);
  }

  // Wi-FiをSTAモードで初期化
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // 💡 【修正】ESP32用の正しい関数でチャンネルを「1」に固定
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  // ESP-NOW 初期化
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ Error initializing ESP-NOW");
    return;
  }

  // 受信コールバックを登録
  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("🟢 Ground Receiver Ready. Listening on Channel 1...");
}

void loop() {
  vTaskDelay(100);
}