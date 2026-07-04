#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include <SPI.h>
#include <SD.h>
#include "secrets.h"

// ---- SDカードSPIピン（Atomic TFCard Base / AtomS3R）----
#define SD_SPI_SCK_PIN  7
#define SD_SPI_MISO_PIN 8
#define SD_SPI_MOSI_PIN 6
#define SD_SPI_CS_PIN   5

const uint16_t UDP_PORT = 9870;

WiFiUDP udp;
File logFile;

#pragma pack(push, 1)
struct ImuPacket {
  uint32_t timestamp_ms;
  float ax, ay, az;
  float gx, gy, gz;
};
#pragma pack(pop)

QueueHandle_t imuQueue;

volatile uint32_t packetsSent = 0;
volatile uint32_t packetsDropped = 0;
volatile bool imuOk = false;
volatile bool sdOk = false;

volatile uint32_t lastImuUs = 0;
volatile uint32_t lastUdpUs = 0;
volatile uint32_t lastSdUs  = 0;
volatile uint32_t maxSdUs   = 0;
bool diagMode = false;
bool streamMode = false; // ← 全加速度データのシリアル出力トグル

// ---------------------------------------------------------
// Core1: IMUサンプリング（100Hz）
// ---------------------------------------------------------
void imuTask(void* arg) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(10); // 100Hz

  while (true) {
    ImuPacket pkt;
    pkt.timestamp_ms = millis();

    uint32_t t0 = micros();
    imuOk = M5.Imu.getAccelData(&pkt.ax, &pkt.ay, &pkt.az)
         && M5.Imu.getGyroData(&pkt.gx, &pkt.gy, &pkt.gz);
    lastImuUs = micros() - t0;

    if (xQueueSend(imuQueue, &pkt, 0) != pdTRUE) {
      packetsDropped++;
    }

    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

// ---------------------------------------------------------
// Core0: UDP送信 + SD書き込み + シリアルストリーム出力
// ---------------------------------------------------------
void ioTask(void* arg) {
  ImuPacket pkt;
  char lineBuf[128];
  uint32_t lastFlush = millis();

  while (true) {
    if (xQueueReceive(imuQueue, &pkt, pdMS_TO_TICKS(10)) == pdTRUE) {

      // --- UDP送信 ---
      uint32_t t0 = micros();
      udp.beginPacket(PC_IP, UDP_PORT);
      udp.write((uint8_t*)&pkt, sizeof(pkt));
      udp.endPacket();
      lastUdpUs = micros() - t0;
      packetsSent++;

      // --- SDカードCSV保存 ---
      if (sdOk) {
        uint32_t t1 = micros();
        int len = snprintf(lineBuf, sizeof(lineBuf),
          "%lu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
          pkt.timestamp_ms, pkt.ax, pkt.ay, pkt.az, pkt.gx, pkt.gy, pkt.gz);
        logFile.write((uint8_t*)lineBuf, len);
        lastSdUs = micros() - t1;

        if (lastSdUs > maxSdUs) maxSdUs = lastSdUs;

        /*if (lastSdUs > 50000) {
          Serial.printf("[WARN] SD write spike: %lu us\n", lastSdUs);
        }*/
      }

      // --- 全加速度データをシリアルにストリーム出力 ---
      if (streamMode) {
        Serial.printf("%lu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
          pkt.timestamp_ms, pkt.ax, pkt.ay, pkt.az, pkt.gx, pkt.gy, pkt.gz);
      }

      if (diagMode) {
        static int counter = 0;
        if (++counter % 20 == 0) {
          Serial.printf("IMU: %lu us, UDP: %lu us, SD: %lu us, dropped: %lu, max SD: %lu us\n",
                        lastImuUs, lastUdpUs, lastSdUs, packetsDropped, maxSdUs);
        }
      }
    }

    vTaskDelay(1);

    if (sdOk && millis() - lastFlush > 500) {
      logFile.flush();
      lastFlush = millis();
    }
  }
}

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

  logFile = SD.open(filename, FILE_WRITE);
  if (!logFile) {
    Serial.print("Failed to open log file: ");
    Serial.println(filename);
    return false;
  }

  logFile.println("timestamp_ms,ax,ay,az,gx,gy,gz");
  logFile.flush();

  Serial.print("SD logging to: ");
  Serial.println(filename);
  return true;
}

// ---------------------------------------------------------
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

  Serial.print("Packets dropped (queue full): ");
  Serial.println(packetsDropped);

  Serial.print("Last IMU read time: ");
  Serial.print(lastImuUs);
  Serial.println(" us");

  Serial.print("Last UDP time: ");
  Serial.print(lastUdpUs);
  Serial.println(" us");

  Serial.print("Last SD write time: ");
  Serial.print(lastSdUs);
  Serial.println(" us");

  Serial.print("Max SD write time (since boot): ");
  Serial.print(maxSdUs);
  Serial.println(" us");

  Serial.print("Queue length: ");
  Serial.println(uxQueueMessagesWaiting(imuQueue));

  Serial.print("Uptime: ");
  Serial.print(millis() / 1000);
  Serial.println(" s");

  Serial.print("Free heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");

  Serial.println("=======================");
}

// ---------------------------------------------------------
void setup() {
  M5.begin();
  M5.Imu.init();
  Serial.begin(115200);

  Serial.println("Booting...");

  sdOk = initSDCard();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.print(".");
  }
  Serial.println(" connected!");

  WiFi.setSleep(false);

  udp.begin(UDP_PORT);

  imuQueue = xQueueCreate(200, sizeof(ImuPacket));

  xTaskCreatePinnedToCore(imuTask, "imu", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(ioTask, "io", 8192, NULL, 1, NULL, 0);

  Serial.println("Ready. Commands: check, flush, diag, stream");
}

void loop() {
  M5.update();

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "check") {
      runSelfCheck();
    } else if (cmd == "flush") {
      logFile.flush();
      Serial.println("Flushed.");
    } else if (cmd == "diag") {
      diagMode = !diagMode;
      Serial.print("Diag mode: ");
      Serial.println(diagMode ? "ON" : "OFF");
    } else if (cmd == "stream") {
      streamMode = !streamMode;
      Serial.print("Stream mode: ");
      Serial.println(streamMode ? "ON" : "OFF");
      if (streamMode) {
        Serial.println("timestamp_ms,ax,ay,az,gx,gy,gz"); // ヘッダー出力
      }
    } else if (cmd.length() > 0) {
      Serial.print("Unknown command: ");
      Serial.println(cmd);
      Serial.println("Available: check, flush, diag, stream");
    }
  }
}