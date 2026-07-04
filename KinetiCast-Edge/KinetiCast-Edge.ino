#include <M5Unified.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiUDP.h>
#include "secrets.h"

const uint16_t UDP_PORT = 9870;

WiFiUDP udp;

#pragma pack(push, 1)
struct ImuPacket {
  uint32_t timestamp_ms;
  float ax, ay, az;
  float gx, gy, gz;
};
#pragma pack(pop)

// セルフチェック用の状態共有変数（volatile必須：タスク間で読むため）
volatile uint32_t packetsSent = 0;
volatile bool imuOk = false;

void imuTask(void* arg) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(2); // 500Hz

  while (true) {
    ImuPacket pkt;
    pkt.timestamp_ms = millis();

    imuOk = M5.Imu.getAccelData(&pkt.ax, &pkt.ay, &pkt.az)
         && M5.Imu.getGyroData(&pkt.gx, &pkt.gy, &pkt.gz);

    udp.beginPacket(PC_IP, UDP_PORT);
    udp.write((uint8_t*)&pkt, sizeof(pkt));
    udp.endPacket();
    packetsSent++;

    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

// ---- セルフチェック処理 ----
void runSelfCheck() {
  Serial.println("===== SELF CHECK =====");

  // Wi-Fi状態
  Serial.print("Wi-Fi: ");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("OK  IP=");
    Serial.print(WiFi.localIP());
    Serial.print("  RSSI=");
    Serial.println(WiFi.RSSI());
  } else {
    Serial.println("NG (not connected)");
  }

  // IMU状態
  Serial.print("IMU: ");
  Serial.println(imuOk ? "OK" : "NG (read failed)");

  // UDP送信カウント
  Serial.print("UDP packets sent: ");
  Serial.println(packetsSent);

  // 稼働時間
  Serial.print("Uptime: ");
  Serial.print(millis() / 1000);
  Serial.println(" s");

  // 空きヒープ（メモリ不足の予兆確認）
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

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    Serial.print(".");
  }
  Serial.println(" connected!");

  udp.begin(UDP_PORT);

  xTaskCreatePinnedToCore(imuTask, "imu", 4096, NULL, 1, NULL, 1);

  Serial.println("Ready. Send 'check' via Serial to run self-check.");
}

void loop() {
  M5.update();

  // シリアル受信チェック
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "check") {
      runSelfCheck();
    } else if (cmd.length() > 0) {
      Serial.print("Unknown command: ");
      Serial.println(cmd);
      Serial.println("Available: check");
    }
  }
}