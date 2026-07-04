#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <SPI.h>
#include <SD.h>
#include "secret.h"

// SDカードSPIピン（Atomic TFCard Base）
#define SD_SCK  7
#define SD_MISO 8
#define SD_MOSI 6
#define SD_CS   -1  // CS未接続の場合は-1、要確認

const int QUEUE_SIZE = 500;

WiFiUDP udp;
SPIClass sdSPI(HSPI);

#pragma pack(push, 1)
struct ImuPacket {
  uint32_t timestamp_ms;
  float ax, ay, az;
  float gx, gy, gz;
};
#pragma pack(pop)

// IMUタスク→SD書き込みタスクへのキュー
QueueHandle_t imuQueue;
File logFile;

void imuTask(void* arg) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(2); // 500Hz

  while (true) {
    ImuPacket pkt;
    pkt.timestamp_ms = millis();
    M5.Imu.getAccelData(&pkt.ax, &pkt.ay, &pkt.az);
    M5.Imu.getGyroData(&pkt.gx, &pkt.gy, &pkt.gz);

    // UDP送信
    udp.beginPacket(PC_IP, UDP_PORT);
    udp.write((uint8_t*)&pkt, sizeof(pkt));
    udp.endPacket();

    // SD書き込みキューへ投入（ブロックしない、満杯なら諦める）
    xQueueSend(imuQueue, &pkt, 0);

    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

void sdWriterTask(void* arg) {
  ImuPacket pkt;
  char line[128];
  uint32_t lastFlush = millis();

  while (true) {
    // キューから取れるだけ取って書き込む
    if (xQueueReceive(imuQueue, &pkt, pdMS_TO_TICKS(10)) == pdTRUE) {
      int len = snprintf(line, sizeof(line), "%lu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                          pkt.timestamp_ms, pkt.ax, pkt.ay, pkt.az,
                          pkt.gx, pkt.gy, pkt.gz);
      logFile.write((uint8_t*)line, len);
    }

    // 100msごとにフラッシュ（毎回flushだと重い）
    if (millis() - lastFlush > 100) {
      logFile.flush();
      lastFlush = millis();
    }
  }
}
void selfCheck() {
  Serial.println("===== Self Check =====");

  // Wi-Fi状態
  Serial.print("WiFi: ");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected, IP=");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Not connected");
  }

  // SDカード状態
  Serial.print("SD Card: ");
  if (SD.cardType() == CARD_NONE) {
    Serial.println("Not detected");
  } else {
    Serial.print("OK, Free space=");
    Serial.print((SD.totalBytes() - SD.usedBytes()) / 1024 / 1024);
    Serial.println("MB");
  }

  // ログファイル状態
  Serial.print("Log file: ");
  if (logFile) {
    Serial.print("Open, size=");
    Serial.print(logFile.size());
    Serial.println(" bytes");
  } else {
    Serial.println("Not open");
  }

  // IMU動作確認（1回読んでみる）
  float ax, ay, az, gx, gy, gz;
  M5.Imu.getAccelData(&ax, &ay, &az);
  M5.Imu.getGyroData(&gx, &gy, &gz);
  Serial.printf("IMU Accel: %.3f, %.3f, %.3f\n", ax, ay, az);
  Serial.printf("IMU Gyro : %.3f, %.3f, %.3f\n", gx, gy, gz);

  // キューの溜まり具合（オーバーフロー監視）
  Serial.print("Queue length: ");
  Serial.print(uxQueueMessagesWaiting(imuQueue));
  Serial.print(" / ");
  Serial.println(QUEUE_SIZE);

  Serial.println("=======================");
}


void setup() {
  Serial.begin(115200);
  delay(1000);
  M5.begin();
  M5.Imu.init();


  imuQueue = xQueueCreate(QUEUE_SIZE, sizeof(ImuPacket));

  // SDカード初期化
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI)) {
    Serial.println("SD Card Mount Failed");
    while (1) delay(1000);
  }

  // ログファイル作成（起動ごとに新規ファイル）
  char filename[32];
  snprintf(filename, sizeof(filename), "/log_%lu.csv", millis());
  logFile = SD.open(filename, FILE_WRITE);
  logFile.println("timestamp_ms,ax,ay,az,gx,gy,gz"); // ヘッダー

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(100);

  udp.begin(UDP_PORT);


  // Core1: IMUサンプリング（優先度高）
  xTaskCreatePinnedToCore(imuTask, "imu", 4096, NULL, 2, NULL, 1);
  // Core0: SD書き込み（優先度低、Wi-Fiと同居だが書き込みは軽い）
  xTaskCreatePinnedToCore(sdWriterTask, "sdwriter", 4096, NULL, 1, NULL, 0);


  selfCheck();
}

void loop() {
  M5.update();

  // シリアル入力チェック
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "check") {
      selfCheck();
    } else {
      Serial.print("Unknown command: ");
      Serial.println(cmd);
    }
  }
}