#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "esp_camera.h"

// ==== 🚀 AtomS3R 完全ピン配置 ====
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
bool espNowOk = false;
bool camOk = false;

QueueHandle_t cameraQueue;
uint16_t imageIdCounter = 0;

// 💡 IMUは今回のカメラ単機能ブランチではスコープ外。
//    フェーズ判定による動的な解像度切り替えは行わず、固定インターバルで送信する。
//    (以前のflightPhaseTaskはM5.Imu経由でI2Cバスを触っており、カメラのSCCBと衝突する原因になっていた)
int sendIntervalMs = 150; // 固定送信間隔。まずはここから実測して調整する

bool initCamera() {
  pinMode(CAM_POWER_ENABLE_PIN, OUTPUT);
  digitalWrite(CAM_POWER_ENABLE_PIN, LOW); // カメラ電源ON
  delay(1000); // 安定化待ち

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = CAM_PIN_D0;
  config.pin_d1       = CAM_PIN_D1;
  config.pin_d2       = CAM_PIN_D2;
  config.pin_d3       = CAM_PIN_D3;
  config.pin_d4       = CAM_PIN_D4;
  config.pin_d5       = CAM_PIN_D5;
  config.pin_d6       = CAM_PIN_D6;
  config.pin_d7       = CAM_PIN_D7;
  config.pin_xclk     = CAM_PIN_XCLK;
  config.pin_pclk     = CAM_PIN_PCLK;
  config.pin_vsync    = CAM_PIN_VSYNC;
  config.pin_href     = CAM_PIN_HREF;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_pwdn     = CAM_PIN_PWDN;
  config.pin_reset    = CAM_PIN_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  // 初期設定 (待機中・地上テスト用。まずは確実に通る軽量設定から)
  config.frame_size   = FRAMESIZE_QVGA; // 320x240
  config.jpeg_quality = 20;             // 数値大=高圧縮/低画質。まずは確実性優先
  config.fb_count     = 2;
  config.grab_mode    = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  return (err == ESP_OK);
}

void captureTask(void *pvParameters) {
  static uint32_t droppedFrames = 0;
  while (true) {
    if (!camOk) { vTaskDelay(1000); continue; }
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
      if (xQueueSend(cameraQueue, &fb, 0) != pdTRUE) {
        // sendTaskが追いついておらずキューが満杯 = このfbは諦めて即返却
        // (以前はxQueueOverwriteで黙って上書き→未returnのままリークしていた)
        esp_camera_fb_return(fb);
        droppedFrames++;
        if (droppedFrames % 50 == 0) {
          Serial.printf("Dropped frames so far: %lu\n", droppedFrames);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void sendTask(void *pvParameters) {
  uint32_t framesSent = 0;
  while (true) {
    camera_fb_t *fb = nullptr;
    if (xQueueReceive(cameraQueue, &fb, portMAX_DELAY)) {
      if (espNowOk) {
        uint16_t totalChunks = (fb->len + CHUNK_SIZE - 1) / CHUNK_SIZE;
        uint16_t sendFailCount = 0;
        for (uint16_t i = 0; i < totalChunks; i++) {
          Packet pkt;
          pkt.image_id = imageIdCounter;
          pkt.total_chunks = totalChunks;
          pkt.chunk_idx = i;
          uint32_t offset = i * CHUNK_SIZE;
          uint16_t currentChunkSize = (fb->len - offset > CHUNK_SIZE) ? CHUNK_SIZE : (fb->len - offset);
          pkt.data_len = currentChunkSize;
          memcpy(pkt.payload, fb->buf + offset, currentChunkSize);
          esp_err_t sendResult = esp_now_send(broadcastAddress, (uint8_t *)&pkt, sizeof(Packet));
          if (sendResult != ESP_OK) sendFailCount++;
          delayMicroseconds(500); 
        }
        framesSent++;
        if (framesSent <= 5 || framesSent % 20 == 0) {
          Serial.printf("frame#%lu sent: %d chunks (%u bytes), send_fail=%d\n",
            framesSent, totalChunks, fb->len, sendFailCount);
        }
        imageIdCounter++;
      } else if (framesSent == 0) {
        Serial.println("WARN: frame captured but espNowOk=false, not sending");
      }
      esp_camera_fb_return(fb);
    }
    vTaskDelay(pdMS_TO_TICKS(sendIntervalMs)); // 固定インターバル
  }
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 2000000;
  cfg.internal_imu = false; // 重要: IMU自動初期化がカメラのSCCB(I2C)と衝突するため無効化
  M5.begin(cfg);
  
  M5.Display.begin();
  M5.Display.setBrightness(120);
  M5.Display.clear(RED);
  M5.Display.setCursor(10, 10);
  M5.Display.setTextColor(WHITE);
  M5.Display.setTextSize(2);
  M5.Display.println("ROCKET");

  camOk = initCamera();
  if(camOk) {
    sensor_t * s = esp_camera_sensor_get();
    if (s != nullptr) {
      s->set_exposure_ctrl(s, 0); // AECオフ（マニュアルシャッター）
      s->set_aec_value(s, 300);   // 屋外用の高速シャッター（暗ければ上げる）
    }
    Serial.println("Camera init OK");
  } else {
    Serial.println("FATAL: Camera init failed. captureTask will spin idle forever.");
  }

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); // ground_receiver.cpp側と明示的に一致させる
  if (esp_now_init() == ESP_OK) {
    espNowOk = true;
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    esp_err_t peerResult = esp_now_add_peer(&peerInfo);
    Serial.printf("ESP-NOW init OK. add_peer result=%d\n", (int)peerResult);
  } else {
    Serial.println("FATAL: esp_now_init failed. espNowOk=false, sendTask will not transmit.");
  }
  Serial.printf("WiFi STA MAC: %s\n", WiFi.macAddress().c_str());
  Serial.printf("camOk=%d espNowOk=%d\n", camOk, espNowOk);

  cameraQueue = xQueueCreate(2, sizeof(camera_fb_t*)); // fb_count=2に合わせる
  
  xTaskCreatePinnedToCore(captureTask, "captureTask", 4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(sendTask, "sendTask", 4096, nullptr, 1, nullptr, 1);
}

void loop() {
  vTaskDelete(NULL);
}