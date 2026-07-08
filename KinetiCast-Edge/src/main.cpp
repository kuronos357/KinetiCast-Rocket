#include <M5Unified.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_now.h>

// ---- ピン配置 (M5AtomS3R準拠) ----
#define SD_SPI_SCK_PIN  7
#define SD_SPI_MISO_PIN 8
#define SD_SPI_MOSI_PIN 6
#define SD_SPI_CS_PIN   5

// ---- データ構造体 (28バイトの軽量パケット) ----
struct __attribute__((packed)) ImuDataPacket {
  uint32_t timestamp;
  float ax, ay, az;
  float gx, gy, gz;
};

// ブロードキャストアドレス
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// グローバル変数
File sdFile;
bool sdOk = false;
bool imuOk = false;

// FreeRTOSキュー (最大200個 = 2秒分のデータを一時保管)
QueueHandle_t imuQueue;
const int QUEUE_LENGTH = 200;

TaskHandle_t imuTaskHandle;
TaskHandle_t sdTaskHandle;

// プロトタイプ宣言
void imuTask(void *pvParameters);
void sdTask(void *pvParameters);
bool initSDCard();

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  cfg.external_rtc = false;
  
  M5.begin(cfg);
  delay(1000);
  Serial.println("\n--- KinetiCast INS Logger Start ---");

  // ==========================================
  // 1. IMU初期化とI2Cバス奪還によるレンジ設定
  // ==========================================
  M5.Imu.begin();
  imuOk = M5.Imu.isEnabled();
  
  if(imuOk) {
    // ⭐️ 前回大成功した「I2C管理権の奪還」ハックを適用！
    M5.In_I2C.release();
    delay(10);

    // I2C経由でBMI270（アドレス0x68）のレジスタへ直接レンジ設定を書き込む
    // [加速度レジスタ ACC_CONF(0x40)] 
    // [ジャイロレジスタ GYR_CONF(0x42)] 
    // ※M5Unified内部の書き換えロジックを直接I2C通信で再現します
    uint8_t acc_conf_data[2] = {0x40, 0xAC}; // ODR=100Hz, BWP=Normal, Range=±16G
    uint8_t gyr_conf_data[2] = {0x42, 0xAC}; // ODR=100Hz, BWP=Normal, Range=±2000dps
    
    M5.In_I2C.writeRegister(0x68, 0x40, acc_conf_data + 1, 1, 400000);
    M5.In_I2C.writeRegister(0x68, 0x42, gyr_conf_data + 1, 1, 400000);
    delay(10);

    // ⭐️ 管理権をM5Unifiedに返還
    M5.In_I2C.begin();
    Serial.println("🟢 IMU Range forced to ±16G / ±2000dps via I2C Hack!");
  } else {
    Serial.println("🔴 IMU Init Failed!");
  }

  // 2. SD初期化
  sdOk = initSDCard();

  // 3. ESP-NOW初期化
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() == ESP_OK) {
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 1; 
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
    Serial.println("🟢 ESP-NOW Ready.");
  } else {
    Serial.println("🔴 ESP-NOW Init Failed!");
  }

  // 4. データ受け渡し用のキューを作成
  imuQueue = xQueueCreate(QUEUE_LENGTH, sizeof(ImuDataPacket));

  // 5. タスクの起動
  // 【Core 1】 IMUサンプリング＆ESP-NOW送信タスク (優先度 最高)
  xTaskCreatePinnedToCore(imuTask, "IMUTask", 4096, NULL, 5, &imuTaskHandle, 1);
  // 【Core 0】 SDカード書き込みタスク (優先度 低め)
  xTaskCreatePinnedToCore(sdTask, "SDTask", 8192, NULL, 2, &sdTaskHandle, 0);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

// 🔴 Core 1: 100Hz 厳格サンプリング
void imuTask(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(10); // 10ms = 100Hz

  ImuDataPacket pkt;

  while (1) {
    vTaskDelayUntil(&xLastWakeTime, xFrequency);

    if (imuOk) {
      M5.Imu.update();
      auto imuRaw = M5.Imu.getImuData();
      
      pkt.timestamp = millis();
      pkt.ax = imuRaw.accel.x;
      pkt.ay = imuRaw.accel.y;
      pkt.az = imuRaw.accel.z;
      pkt.gx = imuRaw.gyro.x;
      pkt.gy = imuRaw.gyro.y;
      pkt.gz = imuRaw.gyro.z;

      xQueueSend(imuQueue, &pkt, 0);
      esp_now_send(broadcastAddress, (uint8_t *)&pkt, sizeof(pkt));
    }
  }
}

// 🔵 Core 0: SDカード バックグラウンド書き込み
void sdTask(void *pvParameters) {
  ImuDataPacket pkt;
  int writeCount = 0;

  while (1) {
    if (xQueueReceive(imuQueue, &pkt, portMAX_DELAY) == pdTRUE) {
      if (sdOk && sdFile) {
        sdFile.printf("%u,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                      pkt.timestamp, 
                      pkt.ax, pkt.ay, pkt.az, 
                      pkt.gx, pkt.gy, pkt.gz);
        writeCount++;

        if (writeCount >= 100) {
          sdFile.flush();
          writeCount = 0;
        }
      }
    }
  }
}

bool initSDCard() {
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 40000000)) { 
    Serial.println("🔴 SD Card Mount Failed");
    return false;
  }
  
  int fileIndex = 1;
  String fileName = "/flight_log_" + String(fileIndex) + ".csv";
  while (SD.exists(fileName)) {
    fileIndex++;
    fileName = "/flight_log_" + String(fileIndex) + ".csv";
  }
  
  sdFile = SD.open(fileName, FILE_WRITE);
  if (!sdFile) return false;
  
  sdFile.println("timestamp,ax,ay,az,gx,gy,gz");
  sdFile.flush();
  Serial.println("🟢 SD Logging to: " + fileName);
  return true;
}