#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include "esp_camera.h"

// ==== 🚀 M5AtomS3R-M12 専用カメラピン配置 ====
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    45
#define CAM_PIN_SIOD    2
#define CAM_PIN_SIOC    1
#define CAM_PIN_D7      42
#define CAM_PIN_D6      41
#define CAM_PIN_D5      40
#define CAM_PIN_D4      39
#define CAM_PIN_D3      38
#define CAM_PIN_D2      7
#define CAM_PIN_D1      6
#define CAM_PIN_D0      5
#define CAM_PIN_VSYNC   48
#define CAM_PIN_HREF    47
#define CAM_PIN_PCLK    46

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

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint16_t globalImageId = 0;
bool camOk = false;

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = CAM_PIN_D0; config.pin_d1 = CAM_PIN_D1;
  config.pin_d2 = CAM_PIN_D2; config.pin_d3 = CAM_PIN_D3;
  config.pin_d4 = CAM_PIN_D4; config.pin_d5 = CAM_PIN_D5;
  config.pin_d6 = CAM_PIN_D6; config.pin_d7 = CAM_PIN_D7;
  config.pin_xclk = CAM_PIN_XCLK; config.pin_pclk = CAM_PIN_PCLK;
  config.pin_vsync = CAM_PIN_VSYNC; config.pin_href = CAM_PIN_HREF;
  config.pin_sccb_sda = CAM_PIN_SIOD; config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_pwdn = CAM_PIN_PWDN; config.pin_reset = CAM_PIN_RESET;
  
  config.xclk_freq_hz = 10000000; // 10MHzで安定重視
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA; 
  config.jpeg_quality = 15; // 圧縮率を少し上げて転送負荷を軽減
  config.fb_count = 1; 
  config.fb_location = CAMERA_FB_IN_PSRAM; 
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;   
  config.sccb_i2c_port = 1;

  return (esp_camera_init(&config) == ESP_OK);
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  delay(1500); // シリアル接続待ちを長めに確保

  Serial.println("\n--- 🚀 ZERObase Rocket Cam Start ---");
  
  camOk = initCamera();
  if (camOk) {
    Serial.println("🟢 CAM INIT: SUCCESS");
  } else {
    Serial.println("❌ CAM INIT: FAILED");
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() == ESP_OK) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
    Serial.println("🟢 ESP-NOW INIT: SUCCESS");
  } else {
    Serial.println("❌ ESP-NOW INIT: FAILED");
  }
}

void loop() {
  if (!camOk) {
    Serial.println("⚠️ Camera not available.");
    delay(1000);
    return;
  }

  // 1フレーム取得
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ Failed to capture frame");
    delay(100);
    return;
  }

  uint16_t total_chunks = (fb->len + CHUNK_SIZE - 1) / CHUNK_SIZE;
  globalImageId++;

  // シリアルにデバッグ出力
  Serial.printf("[TX] Img #%d | Size: %d bytes | Chunks: %d\n", globalImageId, fb->len, total_chunks);

  // パケット分割送信
  Packet pkt;
  for (uint16_t i = 0; i < total_chunks; i++) {
    pkt.image_id = globalImageId;
    pkt.total_chunks = total_chunks;
    pkt.chunk_idx = i;
    
    uint32_t offset = i * CHUNK_SIZE;
    uint32_t remaining = fb->len - offset;
    pkt.data_len = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
    
    memcpy(pkt.payload, fb->buf + offset, pkt.data_len);
    esp_now_send(broadcastAddress, (uint8_t *)&pkt, 7 + pkt.data_len);
    delayMicroseconds(500); // 連続送信による取りこぼし防止のディレイ
  }

  esp_camera_fb_return(fb);
  delay(50); // 次のフレーム撮影までの適度なインターバル
}