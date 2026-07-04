#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <SPI.h>
#include <SD.h>
#include "secrets.h"

const uint16_t UDP_PORT = 9870;

// Atomic TFCard Base SPIピン（AtomS3R用）
#define SD_SPI_SCK_PIN  7
#define SD_SPI_MISO_PIN 8
#define SD_SPI_MOSI_PIN 6
#define SD_SPI_CS_PIN   5

WiFiUDP udp;
File logFile;

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

// SDカードへの書き込みはSPI排他アクセスなので、UDP送信とは別に
// ミューテックスで保護する（Core0/Core1両方から触るなら必須）
SemaphoreHandle_t sdMutex;

void imuTask(void* arg) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(2); // 500Hz

  // ログ用バッファ（毎回SDに書くと遅いので溜めてまとめ書き）
  char lineBuf[128];

  while (true) {
    ImuPacket pkt;
    pkt.timestamp_ms = millis();

    imuOk = M5.Imu.getAccelData(&pkt.ax, &pkt.ay, &pkt.az)
         && M5.Imu.getGyroData(&pkt.gx, &pkt.gy, &pkt.gz);

    // --- UDP送信 ---
    udp.beginPacket(PC_IP, UDP_PORT);
    udp.write((uint8_t*)&pkt, sizeof(pkt));
    udp.endPacket();
    packetsSent++;

    // --- TFカードCSV保存 ---
    if (sdOk) {
      int len = snprintf(lineBuf, sizeof(lineBuf),
        "%lu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
        pkt.timestamp_ms, pkt.ax, pkt.ay, pkt.az, pkt.gx, pkt.gy, pkt.gz);

      if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        logFile.write((uint8_t*)lineBuf, len);
        xSemaphoreGive(sdMutex);
      }
      // 取得できなかった場合はこのサンプルの保存をスキップ
      // （500Hzなので1サンプル欠けても実害は小さい）
    }

    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

bool initSDCard() {
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);

  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    Serial.println("SD Init failed!");
    return false;
  }

  // ファイル名は起動ごとにタイムスタンプ的な連番にしたいが、
  // RTCがないので簡易的に既存ファイル数で連番を振る
  char filename[32];
  int idx = 0;
  do {
    snprintf(filename, sizeof(filename), "/imu_log_%03d.csv", idx);
    idx++;
  } while (SD.exists(filename) && idx < 1000);

  logFile = SD.open(filename, FILE_WRITE);
  if (!logFile) {
    Serial.print("Failed to open log file: ");
    Serial.println(filename);
    return false;
  }

  // CSVヘッダ
  logFile.println("timestamp_ms,ax,ay,az,gx,gy,gz");
  logFile.flush();

  Serial.print("SD logging to: ");
  Serial.println(filename);
  return true;
}

void runSelfCheck() {
  Serial.println("===== SELF CHECK =====");

  Serial.print("Wi-Fi: ");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("OK  IP=");
    Serial.print(WiFi.localIP());
    Serial.print("  RSSI=");
    Serial.println(WiFi.RSSI());
  } else {
    Serial.println("NG (not connected)");
  }

  Serial.print("IMU: ");
  Serial.println(imuOk ? "OK" : "NG (read failed)");

  Serial.print("SD Card: ");
  Serial.println(sdOk ? "OK" : "NG (not initialized)");

  Serial.print("UDP packets sent: ");
  Serial.println(packetsSent);

  Serial.print("Uptime: ");
  Serial.print(millis() / 1000);
  Serial.println(" s");

  Serial.print("Free heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");

  Serial.println("=======================");
}

void setup() {
  M5.begin();
  M5.Imu.init();
  Serial.begin(115200);

  Serial.println("Booting...");

  sdMutex = xSemaphoreCreateMutex();
  sdOk = initSDCard();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.print(".");
  }
  Serial.println(" connected!");

  WiFi.setSleep(false);  // ← 追加：モデムスリープを無効化

  udp.begin(UDP_PORT);

  xTaskCreatePinnedToCore(imuTask, "imu", 4096, NULL, 1, NULL, 1);

  Serial.println("Ready. Send 'check' via Serial to run self-check.");
}

void loop() {
  M5.update();

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "check") {
      runSelfCheck();
    } else if (cmd == "flush") {
      // 手動でSDに書き込みを確定させたい場合
      if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        logFile.flush();
        xSemaphoreGive(sdMutex);
        Serial.println("Flushed.");
      }
    } else if (cmd.length() > 0) {
      Serial.print("Unknown command: ");
      Serial.println(cmd);
      Serial.println("Available: check, flush");
    }
  }
}