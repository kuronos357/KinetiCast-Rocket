#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <SPI.h>
#include <SD.h>
#include "esp_camera.h"
#include "secrets.h"

// =========================================================
// デバッグ用トグル：0にするとカメラ機能を完全に無効化してIMU単体テストができる
// =========================================================
#define ENABLE_CAMERA 1

// ---- SDカードSPIピン（Atomic TFCard Base / AtomS3R）----
#define SD_SPI_SCK_PIN  7
#define SD_SPI_MISO_PIN 8
#define SD_SPI_MOSI_PIN 6
#define SD_SPI_CS_PIN   5

// ---- カメラピン（AtomS3R-M12 / OV3660、M5Stack公式camera_pins.hと一致確認済み）----
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
#define CAM_POWER_ENABLE_PIN 18

const uint16_t UDP_PORT = 9870;

WiFiUDP udp;
File imuLogFile;

#pragma pack(push, 1)
struct ImuPacket {
  uint32_t timestamp_ms;
  float ax, ay, az;
  float gx, gy, gz;
};
#pragma pack(pop)

volatile uint32_t packetsSent = 0;
volatile bool imuOk = false;
volatile bool sdOk = false;
volatile bool camOk = false;
volatile esp_err_t camErrCode = ESP_OK; // カメラ初期化失敗時のエラーコードを保持

// IMUタスク → カメラタスクへ最新加速度を渡す共有変数
volatile float sharedAx = 0, sharedAy = 0, sharedAz = 1.0;

// ---- 上死点検知の状態機械 ----
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

// ---------------------------------------------------------
// Core1: IMUサンプリング（100Hz）+ UDP送信 + SD保存 + 共有変数更新
// ---------------------------------------------------------
void imuTask(void* arg) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(10); // 100Hz
  char lineBuf[128];

  while (true) {
    ImuPacket pkt;
    pkt.timestamp_ms = millis();

    imuOk = M5.Imu.getAccelData(&pkt.ax, &pkt.ay, &pkt.az)
         && M5.Imu.getGyroData(&pkt.gx, &pkt.gy, &pkt.gz);

    sharedAx = pkt.ax;
    sharedAy = pkt.ay;
    sharedAz = pkt.az;

    udp.beginPacket(PC_IP, UDP_PORT);
    udp.write((uint8_t*)&pkt, sizeof(pkt));
    udp.endPacket();
    packetsSent++;

    if (sdOk) {
      int len = snprintf(lineBuf, sizeof(lineBuf),
        "%lu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
        pkt.timestamp_ms, pkt.ax, pkt.ay, pkt.az, pkt.gx, pkt.gy, pkt.gz);
      imuLogFile.write((uint8_t*)lineBuf, len);
    }

    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

#if ENABLE_CAMERA
// ---------------------------------------------------------
// 上死点検知ロジック（カメラタスクから毎ループ呼ばれる）
// ---------------------------------------------------------
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

// ---------------------------------------------------------
// カメラ初期化
// ---------------------------------------------------------
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
  config.frame_size = FRAMESIZE_UXGA;
  config.jpeg_quality = 10;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.sccb_i2c_port = 0; 

  esp_err_t err = esp_camera_init(&config);
  camErrCode = err;
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  Serial.println("Camera initialized (UXGA, JPEG).");
  return true;
}

// ---------------------------------------------------------
// Core0: カメラタスク（定期撮影 + 上死点検知 + 検知時キャプチャ）
// ---------------------------------------------------------
void cameraTask(void* arg) {
  const uint32_t CAPTURE_INTERVAL_MS = 1000;
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
          Serial.print("[CAMERA] Apogee snapshot saved: ");
        } else {
          snprintf(filename, sizeof(filename), "/cam_%04d.jpg", fileIdx++);
        }

        File imgFile = SD.open(filename, FILE_WRITE);
        if (imgFile) {
          imgFile.write(fb->buf, fb->len);
          imgFile.close();
          if (apogeeJustDetected) Serial.println(filename);
        } else {
          Serial.print("Failed to save: ");
          Serial.println(filename);
        }

        esp_camera_fb_return(fb);
        lastCaptureAt = now;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
#endif // ENABLE_CAMERA

// ---------------------------------------------------------
bool initSDCard() {
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    Serial.println("SD Init failed!");
    return false;
  }

  char filename[32];
  int idx = 0;
  do {
    snprintf(filename, sizeof(filename), "/imu_log_%03d.csv", idx);
    idx++;
  } while (SD.exists(filename) && idx < 1000);

  imuLogFile = SD.open(filename, FILE_WRITE);
  if (!imuLogFile) return false;

  imuLogFile.println("timestamp_ms,ax,ay,az,gx,gy,gz");
  imuLogFile.flush();
  Serial.print("SD logging to: ");
  Serial.println(filename);
  return true;
}

// ---------------------------------------------------------
void runSelfCheck() {
  Serial.println("===== SELF CHECK =====");
  Serial.printf("Camera enabled at compile time: %s\n", ENABLE_CAMERA ? "YES" : "NO");
  Serial.print("Wi-Fi: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "OK" : "NG");
  Serial.print("IMU: "); Serial.println(imuOk ? "OK" : "NG");
  Serial.print("SD Card: "); Serial.println(sdOk ? "OK" : "NG");
  Serial.print("Camera: "); Serial.println(camOk ? "OK" : "NG");
  if (!camOk) {
    Serial.printf("Camera error code: 0x%x\n", camErrCode);
  }
  Serial.print("Flight state: ");
  switch (flightState) {
    case WAITING_LAUNCH: Serial.println("WAITING_LAUNCH"); break;
    case POWERED: Serial.println("POWERED"); break;
    case COASTING: Serial.println("COASTING"); break;
    case APOGEE_DETECTED: Serial.println("APOGEE_DETECTED"); break;
  }
  Serial.print("Current accel magnitude: ");
  Serial.println(sqrtf(sharedAx*sharedAx + sharedAy*sharedAy + sharedAz*sharedAz), 3);
  Serial.print("UDP packets sent: "); Serial.println(packetsSent);
  Serial.print("Uptime: "); Serial.print(millis()/1000); Serial.println(" s");
  Serial.print("Free heap: "); Serial.println(ESP.getFreeHeap());
  Serial.println("=======================");
}

// ---------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(3000); // シリアルモニタが接続するまで待つ(起動直後のログを見逃さないため)
  Serial.println("Booting...");

#if ENABLE_CAMERA
  // 最優先：M5.begin()より前にGPIO18をLOWにする
  pinMode(CAM_POWER_ENABLE_PIN, OUTPUT);
  digitalWrite(CAM_POWER_ENABLE_PIN, LOW);
  delay(500);
  Serial.println("GPIO18 set LOW (camera power enable).");
#endif

  M5.begin();
  bool imuInitOk = M5.Imu.init();
  Serial.print("M5.Imu.init() result: ");
  Serial.println(imuInitOk ? "OK" : "FAILED");

  sdOk = initSDCard();

#if ENABLE_CAMERA
  camOk = initCamera();
#else
  camOk = false;
  Serial.println("Camera disabled for this build (ENABLE_CAMERA=0).");
#endif

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(100); Serial.print("."); }
  Serial.println(" connected!");
  WiFi.setSleep(false);

  udp.begin(UDP_PORT);

  xTaskCreatePinnedToCore(imuTask, "imu", 4096, NULL, 2, NULL, 1);

#if ENABLE_CAMERA
  xTaskCreatePinnedToCore(cameraTask, "camera", 8192, NULL, 1, NULL, 0);
#endif

  runSelfCheck();
  Serial.println("Ready. Commands: check, flush");
}

void loop() {
  M5.update();
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "check") {
      runSelfCheck();
    } else if (cmd == "flush") {
      imuLogFile.flush();
      Serial.println("Flushed.");
    } else if (cmd.length() > 0) {
      Serial.println("Unknown command. Available: check, flush");
    }
  }
}