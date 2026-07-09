#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include "esp_camera.h"

// ==== 🚀 過去の動作実績に基づく AtomS3R 完全ピン配置 ====
#define CAM_POWER_ENABLE_PIN 18
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    21
#define CAM_PIN_SIOD    12
#define CAM_PIN_SIOC    9

#define CAM_PIN_D7      13
#define CAM_PIN_D6      11
#define CAM_PIN_D5      17
#define CAM_PIN_D4      4
#define CAM_PIN_D3      48
#define CAM_PIN_D2      46
#define CAM_PIN_D1      42
#define CAM_PIN_D0      3

#define CAM_PIN_VSYNC   10
#define CAM_PIN_HREF    14
#define CAM_PIN_PCLK    40

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

QueueHandle_t cameraQueue = nullptr;
volatile bool camOk = false;
volatile bool espNowOk = false;
volatile uint32_t packetsSent = 0;
volatile uint16_t globalImageId = 0;

// 💡 セルフチェック関数
void runSelfCheck() {
  Serial.println("\n===== 🛠️ ROCKET SELF CHECK =====");
  Serial.print("  Camera Status : "); Serial.println(camOk ? "🟢 OK" : "❌ FAILED/NG");
  Serial.print("  ESP-NOW Status: "); Serial.println(espNowOk ? "🟢 OK" : "❌ FAILED/NG");
  Serial.print("  Sent Images   : "); Serial.println(globalImageId);
  Serial.print("  Sent Packets  : "); Serial.println(packetsSent);
  Serial.print("  Uptime        : "); Serial.print(millis() / 1000); Serial.println(" sec");
  Serial.print("  Free Heap     : "); Serial.print(ESP.getFreeHeap()); Serial.println(" Bytes");
  Serial.print("  Free PSRAM    : "); Serial.print(ESP.getFreePsram()); Serial.println(" Bytes");
  Serial.println("================================\n");
}

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0  = CAM_PIN_D0;
  config.pin_d1  = CAM_PIN_D1;
  config.pin_d2  = CAM_PIN_D2;
  config.pin_d3  = CAM_PIN_D3;
  config.pin_d4  = CAM_PIN_D4;
  config.pin_d5  = CAM_PIN_D5;
  config.pin_d6  = CAM_PIN_D6;
  config.pin_d7  = CAM_PIN_D7;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_pclk = CAM_PIN_PCLK;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href  = CAM_PIN_HREF;
  config.pin_sscb_sda = CAM_PIN_SIOD;
  config.pin_sscb_scl = CAM_PIN_SIOC;
  config.pin_pwdn  = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.xclk_freq_hz = 20000000;
  
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA; 
  config.jpeg_quality = 12;
  config.fb_count = 2; 
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.sccb_i2c_port = 1; 

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  return true;
}

void sendTask(void* arg) {
  Packet packet;
  while (true) {
    camera_fb_t* fb = nullptr;
    if (xQueueReceive(cameraQueue, &fb, portMAX_DELAY) == pdTRUE) {
      if (fb && fb->len > 0) {
        uint32_t totalBytes = fb->len;
        uint16_t totalChunks = (totalBytes + CHUNK_SIZE - 1) / CHUNK_SIZE;
        uint16_t imgId = globalImageId++;
        
        uint32_t offset = 0;
        for (uint16_t i = 0; i < totalChunks; i++) {
          uint8_t currentChunkSize = CHUNK_SIZE;
          if (offset + CHUNK_SIZE > totalBytes) {
            currentChunkSize = totalBytes - offset;
          }
          
          packet.image_id = imgId;
          packet.total_chunks = totalChunks;
          packet.chunk_idx = i;
          packet.data_len = currentChunkSize;
          memcpy(packet.payload, fb->buf + offset, currentChunkSize);
          
          if (esp_now_send(broadcastAddress, (uint8_t*)&packet, sizeof(Packet)) == ESP_OK) {
            packetsSent++;
          }
          offset += currentChunkSize;
          
          delayMicroseconds(500); 
        }
      }
      if (fb) {
        esp_camera_fb_return(fb);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(250)); // 約4fps
  }
}

void captureTask(void* arg) {
  while (true) {
    if (!camOk) { 
      Serial.println("[WARNING] Camera is NOT initialized! Check hardware.");
      vTaskDelay(pdMS_TO_TICKS(1000)); 
      continue; 
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      if (xQueueSend(cameraQueue, &fb, 0) != pdTRUE) {
        esp_camera_fb_return(fb);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50)); 
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000); 
  Serial.println("Booting Rocket Cam...");

  // 1. カメラ電源ピン(GPIO18)物理ON
  pinMode(CAM_POWER_ENABLE_PIN, OUTPUT);
  digitalWrite(CAM_POWER_ENABLE_PIN, LOW);
  delay(500);
  Serial.println("GPIO18 set LOW (camera power enable).");

  // 2. M5メイン初期化
  auto cfg = M5.config();
  M5.begin(cfg);

  // 💡 M5Unifiedが占有したI2Cポートを解放する魔改造コード（前回の対策）
  Wire1.end(); 
  delay(100);

  Serial.println("\n============ 🚀 AtomS3R ESP-NOW CAM START ============");
  
  // 3. カメラ初期化
  camOk = initCamera();
  if(camOk) {
    Serial.println("🟢 Camera Hardware Init: SUCCESS");
  } else {
    Serial.println("❌ Camera Hardware Init: FAILED");
  }

  // 4. ESP-NOW 初期化
  WiFi.mode(WIFI_STA);
  if (esp_now_init() == ESP_OK) {
    espNowOk = true;
    Serial.println("🟢 ESP-NOW INIT: SUCCESS");
  } else {
    Serial.println("❌ ESP-NOW Init Failed");
    return;
  }

  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("❌ Failed to add peer");
    return;
  }

  cameraQueue = xQueueCreate(2, sizeof(camera_fb_t*));
  xTaskCreatePinnedToCore(captureTask, "captureTask", 4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(sendTask, "sendTask", 4096, nullptr, 1, nullptr, 0);

  // 起動時に1回自動でセルフチェックを表示
  runSelfCheck();
  Serial.println("Ready. Type 'check' to display status.");
}

void loop() {
  M5.update();
  
  // 💡 シリアル通信のコマンド待機
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim(); // 改行コードなどのトリミング
    if (cmd == "check") {
      runSelfCheck();
    } else if (cmd.length() > 0) {
      Serial.println("❓ Unknown command. Type 'check'");
    }
  }
  delay(10);
}