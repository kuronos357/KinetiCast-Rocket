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

// コア間で画像ポインタを受け渡すためのFreeRTOSキュー
QueueHandle_t cameraQueue = NULL;

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
  
  // 💡 AtomS3Rはクロックを20MHzで安定駆動できます
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  // 💡ロケット用途の解像度設定
  // 確実に毎秒の枚数を稼ぎたい場合は FRAMESIZE_SVGA (800x600) もしくは FRAMESIZE_VGA (640x480) が超おすすめ
  config.frame_size = FRAMESIZE_SVGA; 
  config.jpeg_quality = 12; // 10〜15あたりが画質と軽さのバランスが良いです
  
  // 💡並列パイプライン処理のため、バッファは2コマ分確保
  config.fb_count = 2; 
  config.fb_location = CAMERA_FB_IN_PSRAM; // AtomS3Rの高速な拡張RAMを活用
  config.grab_mode = CAMERA_GRAB_LATEST;   // 常に最新のコマを掴む
  config.sccb_i2c_port = 1;

  return (esp_camera_init(&config) == ESP_OK);
}

// =========================================================================
// 【Core 0】 ESP-NOW送信タスク (Wi-Fi/通信担当)
// =========================================================================
void sendTask(void* arg) {
  Packet pkt;
  camera_fb_t* fb = nullptr;

  while (true) {
    // Core 1(撮影)から画像ポインタが回ってくるのを待つ
    if (xQueueReceive(cameraQueue, &fb, portMAX_DELAY) == pdTRUE) {
      if (fb) {
        uint16_t total_chunks = (fb->len + CHUNK_SIZE - 1) / CHUNK_SIZE;
        globalImageId++;

        for (uint16_t i = 0; i < total_chunks; i++) {
          pkt.image_id = globalImageId;
          pkt.total_chunks = total_chunks;
          pkt.chunk_idx = i;
          
          uint32_t offset = i * CHUNK_SIZE;
          uint32_t remaining = fb->len - offset;
          pkt.data_len = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
          
          memcpy(pkt.payload, fb->buf + offset, pkt.data_len);

          // Wi-FiコアであるCore 0自身から直接叩くので高効率
          esp_now_send(broadcastAddress, (uint8_t *)&pkt, 7 + pkt.data_len);
          
          // ESP32-S3の内部バッファ溢れを防ぐわずかなウェイト
          delayMicroseconds(300); 
        }
        // 送信が終わったらカメラドライバにバッファを返却
        esp_camera_fb_return(fb);
      }
    }
  }
}

// =========================================================================
// 【Core 1】 カメラ撮影タスク (メイン制御担当)
// =========================================================================
void captureTask(void* arg) {
  while (true) {
    if (!camOk) { vTaskDelay(10); continue; }

    // カメラからフレームを取得
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      // 送信タスク(Core 0)へポインタを渡す
      // タイムアウトを0にすることで、もし通信側がまだ送信中で詰まっていたら
      // このコマをその場で破棄して、リアルタイム性を最優先（最新フレームを維持）します
      if (xQueueSend(cameraQueue, &fb, 0) != pdTRUE) {
        esp_camera_fb_return(fb);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5)); // ループの僅かなウェイト
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  
  // カメラ初期化
  camOk = initCamera();

  // ESP-NOW 初期化
  WiFi.mode(WIFI_STA);
  if (esp_now_init() == ESP_OK) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
  }

  if (camOk) {
    // 最新フレームを1個だけホールドするキューを作成
    cameraQueue = xQueueCreate(1, sizeof(camera_fb_t*));

    // タスクを別々のコアにピン留めして起動
    xTaskCreatePinnedToCore(sendTask, "sendTask", 4096, NULL, 1, NULL, 0);       // Core 0
    xTaskCreatePinnedToCore(captureTask, "captureTask", 4096, NULL, 1, NULL, 1); // Core 1
    
    Serial.println("🚀 AtomS3R-M12 Dual-core logic started.");
  } else {
    Serial.println("❌ Camera init failed.");
  }
}

void loop() {
  // メインループ（Core 1）はフリーです。
  vTaskDelay(1000);
}