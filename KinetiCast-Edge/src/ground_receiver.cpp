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

// 💡 AtomS3RのPSRAMを活かし、受信バッファを300KBに拡大！(VGAサイズや最高画質でも余裕)
#define MAX_IMG_SIZE (300 * 1024)
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

void updateDisplay() {
  M5.Display.startWrite();
  M5.Display.setCursor(5, 5);
  M5.Display.printf("=== GND S3R ===");
  M5.Display.setCursor(5, 25);
  M5.Display.printf("ImgID:%05d", currentImageId == 0xFFFF ? 0 : currentImageId);
  M5.Display.setCursor(5, 45);
  M5.Display.printf("Pkts :%d", totalPacketsRecv);
  M5.Display.setCursor(5, 65);
  M5.Display.printf("Saved:%d", totalImagesSaved);
  
  // 画面下部に進捗バー
  M5.Display.fillRect(5, 95, 118, 10, BLACK);
  M5.Display.drawRect(5, 95, 118, 10, WHITE);
  if (currentImageId != 0xFFFF && chunksReceived > 0) {
     int width = (chunksReceived * 118) / 100; 
     if(width > 118) width = 118;
     M5.Display.fillRect(5, 95, width, 10, GREEN);
  }
  M5.Display.endWrite();
}

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  totalPacketsRecv++;
  if (len != sizeof(Packet)) return;

  Packet packet;
  memcpy(&packet, incomingData, sizeof(Packet));

  if (currentImageId != packet.image_id) {
    currentImageId = packet.image_id;
    chunksReceived = 0;
    maxOffsetDetected = 0;
    memset(imgBuffer, 0, MAX_IMG_SIZE);
  }

  uint32_t offset = (uint32_t)packet.chunk_idx * CHUNK_SIZE;
  
  if (offset + packet.data_len <= MAX_IMG_SIZE) {
    memcpy(imgBuffer + offset, packet.payload, packet.data_len);
    chunksReceived++;

    uint32_t endPos = offset + packet.data_len;
    if (endPos > maxOffsetDetected) {
      maxOffsetDetected = endPos;
    }

    if (chunksReceived >= packet.total_chunks) {
      sendToPC(maxOffsetDetected);
      totalImagesSaved++;
      currentImageId = 0xFFFF; 
      chunksReceived = 0;
      updateDisplay();
    }
  }
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  cfg.external_rtc = false;
  M5.begin(cfg);
  
  delay(1000); 

  M5.Display.begin();
  M5.Display.setBrightness(120); 
  M5.Display.setTextSize(1); 
  M5.Display.setTextColor(GREEN, BLACK);
  M5.Display.clear(BLACK);
  M5.Display.println("Booting GND S3R...");

  Serial.println("\n============ 📡 AtomS3R GROUND START ============");

  // 💡 【最重要】AtomS3Rの「PSRAM（SPIRAM）」領域から安全に300KBを確保
  imgBuffer = (uint8_t*)heap_caps_malloc(MAX_IMG_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (imgBuffer == nullptr) {
    M5.Display.println("❌ PSRAM Malloc Fail");
    Serial.println("❌ PSRAM Malloc Fail");
    while(1) delay(10);
  }
  M5.Display.println("PSRAM Buffer OK.");

  WiFi.mode(WIFI_STA);
  delay(100);

  if (esp_now_init() != ESP_OK) {
    M5.Display.println("❌ ESPNow Fail");
    Serial.println("❌ ESP-NOW Init Failed");
    return;
  }

  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
  M5.Display.clear(BLACK);
  updateDisplay();
}

void loop() {
  M5.update();
  static uint32_t lastUpdate = 0;
  if (millis() - lastUpdate > 1000) {
    lastUpdate = millis();
    updateDisplay();
  }
  delay(10);
}