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

#define MAX_IMG_SIZE (300 * 1024) // 300KB
uint8_t* imgBuffer = nullptr;

uint16_t currentImageId = 0xFFFF;
uint16_t chunksReceived = 0;
uint32_t maxOffsetDetected = 0;

void sendToPC(uint32_t dataLen) {
  if (dataLen == 0) return;
  Serial.write("IMG:");
  Serial.write((uint8_t*)&dataLen, 4);
  Serial.write(imgBuffer, dataLen);
  Serial.flush();
}

void onDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  if (len != sizeof(Packet)) return;
  Packet pkt;
  memcpy(&pkt, incomingData, sizeof(Packet));

  if (pkt.image_id != currentImageId) {
    currentImageId = pkt.image_id;
    chunksReceived = 0;
    maxOffsetDetected = 0;
  }

  uint32_t offset = pkt.chunk_idx * CHUNK_SIZE;
  if (offset + pkt.data_len < MAX_IMG_SIZE) {
    memcpy(imgBuffer + offset, pkt.payload, pkt.data_len);
    chunksReceived++;
    if (offset + pkt.data_len > maxOffsetDetected) {
      maxOffsetDetected = offset + pkt.data_len;
    }
    
    if (chunksReceived >= pkt.total_chunks) {
      sendToPC(maxOffsetDetected);
      currentImageId = 0xFFFF; 
      chunksReceived = 0;
    }
  }
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 2000000;
  M5.begin(cfg);
  
  M5.Display.begin();
  M5.Display.setBrightness(120); 
  M5.Display.clear(BLUE);
  M5.Display.setCursor(10, 10);
  M5.Display.setTextColor(WHITE);
  M5.Display.setTextSize(2);
  M5.Display.println("GROUND");

  imgBuffer = (uint8_t*)heap_caps_malloc(MAX_IMG_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  
  WiFi.mode(WIFI_STA);
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
  
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onDataRecv);
  }
}

void loop() {
  vTaskDelete(NULL);
}