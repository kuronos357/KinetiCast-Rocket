#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

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

#define MAX_IMG_SIZE (200 * 1024)
uint8_t* imgBuffer = nullptr;

uint16_t currentImageId = 0xFFFF;
uint16_t chunksReceived = 0;
uint32_t maxOffsetDetected = 0;

uint32_t totalImagesSaved = 0;
uint32_t totalPacketsRecv = 0;

void sendToPC(uint32_t dataLen) {
  if (dataLen == 0) return;
  Serial.write("IMG:");
  Serial.write((uint8_t*)&dataLen, 4);
  Serial.write(imgBuffer, dataLen);
  Serial.flush();
}

void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  if (len < 7 || imgBuffer == nullptr) return;

  Packet pkt;
  memcpy(&pkt, incomingData, len);
  totalPacketsRecv++;

  if (pkt.image_id != currentImageId) {
    if (currentImageId != 0xFFFF && chunksReceived > 0) {
      sendToPC(maxOffsetDetected);
      totalImagesSaved++;
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

    if (chunksReceived >= pkt.total_chunks) {
      sendToPC(maxOffsetDetected);
      totalImagesSaved++;
      currentImageId = 0xFFFF; 
      chunksReceived = 0;
    }
  }
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  // 電力を食うスピーカーやマイク、不要なRTCは完全に物理OFFにして省電力化
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  cfg.external_rtc = false;
  M5.begin(cfg);
  
  delay(1500);

  // 液晶ディスプレイの王道初期化
  M5.Display.begin();
  M5.Display.setBrightness(80);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(GREEN, BLACK);
  M5.Display.clear(BLACK);
  
  Serial.println("\n--- 📡 ZERObase Ground Station Start ---");

  imgBuffer = (uint8_t*)malloc(MAX_IMG_SIZE);
  if (imgBuffer == nullptr) {
    Serial.println("❌ BUFFER MALLOC FAILED");
    M5.Display.println("❌ MALLOC FAILED");
    while(1) delay(10);
  }

  // Wi-Fiを安全な手順でチャンネル1に初期化
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  delay(50);
  WiFi.disconnect();

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(OnDataRecv);
    Serial.println("🟢 ESP-NOW RECEIVER: READY");
  } else {
    Serial.println("❌ ESP-NOW RECEIVER: FAILED");
    M5.Display.println("❌ ESP-NOW FAILED");
  }
}

void loop() {
  // 画面の部分書き換え（最速で状態を画面に反映）
  M5.Display.startWrite();
  M5.Display.setCursor(10, 20);
  M5.Display.printf("=== ZERO-BASE GROUND ===");
  
  M5.Display.setCursor(10, 60);
  M5.Display.printf("Img Recv : %d frames  ", totalImagesSaved); 
  
  M5.Display.setCursor(10, 100);
  M5.Display.printf("Active ID: #%d       ", currentImageId == 0xFFFF ? 0 : currentImageId);
  
  M5.Display.setCursor(10, 140);
  M5.Display.printf("Pkt Total: %d pcs    ", totalPacketsRecv);
  
  M5.Display.setCursor(10, 180);
  M5.Display.printf("Station  : ALIVE     ");
  M5.Display.endWrite();

  vTaskDelay(pdMS_TO_TICKS(100)); // 0.1秒周期で画面を高速追従
}