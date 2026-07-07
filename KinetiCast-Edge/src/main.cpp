#include <M5Unified.h> 
#include <WiFi.h>
#include <WiFiUDP.h>
#include <SPI.h>
#include <SD.h>
#include "esp_camera.h"
#include "secrets.h"

#define ENABLE_CAMERA 1

// ---- SDカード SPIピン配置 ----
#define SD_SPI_SCK_PIN  7
#define SD_SPI_MISO_PIN 8
#define SD_SPI_MOSI_PIN 6
#define SD_SPI_CS_PIN   5

// ---- OV3660 カメラピン配置 ----
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
#define CAM_PIN_D46     46 
#define CAM_PIN_D2      46
#define CAM_PIN_D1      42
#define CAM_PIN_D0      3
#define CAM_PIN_VSYNC   10
#define CAM_PIN_HREF    14
#define CAM_PIN_PCLK    40
#define CAM_POWER_ENABLE_PIN 18

const uint16_t UDP_PORT = 9870;

WiFiUDP udp;
File imuLogFile;

// ---- 9軸対応 パケットデータ構造体 (計40バイト) ----
#pragma pack(push, 1)
struct ImuPacket {
  uint32_t timestamp_ms;
  float ax, ay, az; 
  float gx, gy, gz; 
  float mx, my, mz; 
};
#pragma pack(pop)

volatile uint32_t packetsSent = 0;
volatile bool imuOk = false;
volatile bool sdOk = false;
volatile bool camOk = false;
volatile esp_err_t camErrCode = ESP_OK;

volatile float sharedAx = 0, sharedAy = 0, sharedAz = 1.0;

enum FlightState {
  WAITING_LAUNCH,
  POWERED,
  COASTING,
  APOGEE_DETECTED
};
volatile FlightState flightState = WAITING_LAUNCH;

const float LAUNCH_THRESHOLD_G   = 3.0;
const float APOGEE_THRESHOLD_G   = 0.2;
const uint32_t APOGEE_SUSTAIN_MS = 300;
const uint32_t MIN_COAST_TIME_MS = 500;

uint32_t stateEnteredAt = 0;
uint32_t apogeeCandidateSince = 0;

// =========================================================================
// [Task 1] 9軸IMU・地磁気サンプリング＆通信タスク (Core 1 / 100Hz)
// =========================================================================
void imuTask(void* arg) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(10); 
  char lineBuf[180]; 

  while (true) {
    M5.update(); 

    ImuPacket pkt;
    pkt.timestamp_ms = millis();

    if (M5.Imu.update()) {
      auto imu_data = M5.Imu.getImuData();
      
      pkt.ax = imu_data.accel.x;
      pkt.ay = imu_data.accel.y;
      pkt.az = imu_data.accel.z;
      
      pkt.gx = imu_data.gyro.x;
      pkt.gy = imu_data.gyro.y;
      pkt.gz = imu_data.gyro.z;
      
      pkt.mx = imu_data.mag.x;
      pkt.my = imu_data.mag.y;
      pkt.mz = imu_data.mag.z;
      
      imuOk = true;
    } else {
      imuOk = M5.Imu.isEnabled();
    }

    sharedAx = pkt.ax;
    sharedAy = pkt.ay;
    sharedAz = pkt.az;

    // 【セーフティガード】WiFiが接続中かつIPが取れている場合のみUDPを送信（エラー12対策）
    if (WiFi.status() == WL_CONNECTED) {
      udp.beginPacket(PC_IP, UDP_PORT);
      udp.write((uint8_t*)&pkt, sizeof(pkt));
      udp.endPacket();
      packetsSent++;
    }

    if (sdOk && imuOk) {
      int len = snprintf(lineBuf, sizeof(lineBuf),
        "%lu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
        pkt.timestamp_ms, pkt.ax, pkt.ay, pkt.az, pkt.gx, pkt.gy, pkt.gz, pkt.mx, pkt.my, pkt.mz);
      imuLogFile.write((uint8_t*)lineBuf, len);
    }

    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

// =========================================================================
// [Task 2] カメラ制御・飛行状態判定タスク (Core 0)
// =========================================================================
#if ENABLE_CAMERA
bool updateFlightState() {
  float ax = sharedAx, ay = sharedAy, az = sharedAz;
  float mag = sqrtf(ax * ax + ay * ay + az * az);
  uint32_t now = millis();

  switch (flightState) {
    case WAITING_LAUNCH:
      if (mag > LAUNCH_THRESHOLD_G) {
        flightState = POWERED;
        stateEnteredAt = now;
        Serial.println("[FLIGHT] Launch detected -> POWERED");
      }
      break;
    case POWERED:
      if (now - stateEnteredAt > MIN_COAST_TIME_MS && mag < LAUNCH_THRESHOLD_G) {
        flightState = COASTING;
        stateEnteredAt = now;
        Serial.println("[FLIGHT] Burnout detected -> COASTING");
      }
      break;
    case COASTING:
      if (mag < APOGEE_THRESHOLD_G) {
        if (apogeeCandidateSince == 0) {
          apogeeCandidateSince = now;
        } else if (now - apogeeCandidateSince > APOGEE_SUSTAIN_MS) {
          flightState = APOGEE_DETECTED;
          Serial.println("[FLIGHT] Apogee confirmed -> APOGEE_DETECTED");
          return true; 
        }
      } else {
        apogeeCandidateSince = 0;
      }
      break;
    case APOGEE_DETECTED:
      break;
  }
  return false;
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
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_pwdn  = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_UXGA; 
  config.jpeg_quality = 10;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.sccb_i2c_port = 0;

  // ⭐️【最重要ガード】M5UnifiedからI2Cバスの管理権を一時的にはく奪する
  M5.In_I2C.release();
  delay(10);

  esp_err_t err = esp_camera_init(&config);
  camErrCode = err;

  // ⭐️【管理権の返還】カメラ初期化が終わったら、IMU通信のためにM5UnifiedへI2Cを戻す
  M5.In_I2C.begin();

  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  Serial.println("Camera initialized (UXGA, JPEG).");
  return true;
}

void cameraTask(void* arg) {
  const uint32_t CAPTURE_INTERVAL_MS = 2000;
  uint32_t lastCaptureAt = 0;
  int fileIdx = 0;

  while (true) {
    bool apogeeJustDetected = updateFlightState();
    uint32_t now = millis();
    bool shouldCaptureRegular = camOk && (now - lastCaptureAt >= CAPTURE_INTERVAL_MS);

    if (shouldCaptureRegular || apogeeJustDetected) {
      camera_fb_t* fb = esp_camera_fb_get();
      if (fb) {
        char filename[40];
        if (apogeeJustDetected) {
          snprintf(filename, sizeof(filename), "/apogee_%lu.jpg", now);
        } else {
          snprintf(filename, sizeof(filename), "/cam_%04d.jpg", fileIdx++);
        }

        File imgFile = SD.open(filename, FILE_WRITE);
        if (imgFile) {
          imgFile.write(fb->buf, fb->len);
          imgFile.close();
        }
        esp_camera_fb_return(fb);
        lastCaptureAt = now;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
#endif

bool initSDCard() {
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) return false;

  char filename[32];
  int idx = 0;
  do {
    snprintf(filename, sizeof(filename), "/imu_log_%03d.csv", idx);
    idx++;
  } while (SD.exists(filename) && idx < 1000);

  imuLogFile = SD.open(filename, FILE_WRITE);
  if (!imuLogFile) return false;

  imuLogFile.println("timestamp_ms,ax,ay,az,gx,gy,gz,mx,my,mz");
  imuLogFile.flush();
  return true;
}

void runSelfCheck() {
  Serial.println("\n===== 🚀 KinetiCast 9軸統合システム SELF CHECK =====");
  
  int boardType = M5.getBoard();
  Serial.printf("基板タイプ (Board Type)    : %d ", boardType);
  if (boardType == 18) Serial.println("(M5AtomS3R として認識)");
  else if (boardType == 145) Serial.println("(M5AtomS3RCam として自動認識成功)");
  else Serial.println("(カスタム自動認識コード)");

  Serial.printf("9軸IMU/地磁気 (M5Unified)  : %s\n", imuOk ? "🟢 OK (有効)" : "🔴 NG (初期化失敗)");
  Serial.printf("SDカード保存用SPIバス      : %s\n", sdOk ? "🟢 OK (マウント完了)" : "🔴 NG (エラー)");
  
#if ENABLE_CAMERA
  Serial.printf("OV3660 カメラユニット     : %s\n", camOk ? "🟢 OK (初期化完了)" : "🔴 NG (カメラエラー)");
  if (!camOk) {
    Serial.printf("  ➔ カメラ初期化エラーコード: 0x%x\n", camErrCode);
  }
#endif

  // タイミングによる「同期待機中」を避けるため、少し待ってリトライをかける
  if (imuOk) {
    for (int i = 0; i < 5; i++) {
      M5.update();
      if (M5.Imu.update()) {
        auto imu_data = M5.Imu.getImuData();
        Serial.printf("  ➔ [地磁気初期応答] Mag X:%6.2f, Y:%6.2f, Z:%6.2f uT\n", 
                      imu_data.mag.x, imu_data.mag.y, imu_data.mag.z);
        break;
      }
      delay(50);
    }
  }
  Serial.println("=====================================================\n");
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  
  // 不要な内部モジュールの自動初期化を完全遮断
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  cfg.external_rtc = false;
  
  M5.begin(cfg);
  delay(500);
  Serial.println("M5Unified core initialized.");

#if ENABLE_CAMERA
  pinMode(CAM_POWER_ENABLE_PIN, OUTPUT);
  digitalWrite(CAM_POWER_ENABLE_PIN, LOW);
  delay(500);
#endif

  // 最初はM5Unifiedの管理下でIMUを起動
  M5.Imu.begin();
  imuOk = M5.Imu.isEnabled();

  // SDカード初期化
  sdOk = initSDCard();

#if ENABLE_CAMERA
  // 先にカメラを初期化（内部でI2C解放・再結合が行われます）
  camOk = initCamera(); 
  delay(200); // ➔ ドライバの衝突ログ抑制のための猶予
#endif

  // 最後にWiFiを接続
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(100); }
  
  // ⭐️【エラー12対策】WiFi接続後、ESP32のネットワークが完全にルーティングを確立するまで2秒待つ
  delay(2000); 
  udp.begin(UDP_PORT);

  // ネットワークが完全に準備できてから高頻度タスクを起動する
// FreeRTOSマルチタスク割り当て (優先度を1に下げてネットワークに道を譲る)
  xTaskCreatePinnedToCore(imuTask, "imu", 4096, NULL, 1, NULL, 1); 
#if ENABLE_CAMERA
  xTaskCreatePinnedToCore(cameraTask, "camera", 8192, NULL, 1, NULL, 0);
#endif

  // 詳細セルフチェックの実行
  runSelfCheck();
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "check") runSelfCheck();
    else if (cmd == "flush") { imuLogFile.flush(); Serial.println("Flushed."); }
  }
}