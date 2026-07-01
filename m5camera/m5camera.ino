#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiUDP.h>

// Wi-Fi設定
const char* SSID = "your_ssid";
const char* PASS = "your_pass";

// UDP設定
const char* PC_IP = "192.168.x.x";
const uint16_t UDP_PORT = 9870;

WiFiUDP udp;

// IMUデータ構造体（パース楽にするためpackedで固める）
#pragma pack(push, 1)
struct ImuPacket {
  uint32_t timestamp_ms;
  float ax, ay, az;  // 加速度 [G]
  float gx, gy, gz;  // ジャイロ [dps]
};
#pragma pack(pop)

void imuTask(void* arg) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(2); // 500Hz

  while (true) {
    ImuPacket pkt;
    pkt.timestamp_ms = millis();
    M5.Imu.getAccelData(&pkt.ax, &pkt.ay, &pkt.az);
    M5.Imu.getGyroData(&pkt.gx, &pkt.gy, &pkt.gz);

    udp.beginPacket(PC_IP, UDP_PORT);
    udp.write((uint8_t*)&pkt, sizeof(pkt));
    udp.endPacket();

    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

void setup() {
  M5.begin();
  M5.Imu.init();

  WiFi.begin(SSID, PASS);
  while (WiFi.status() != WL_CONNECTED) delay(100);

  udp.begin(UDP_PORT);

  // Core 1でIMUタスク起動
  xTaskCreatePinnedToCore(imuTask, "imu", 4096, NULL, 1, NULL, 1);
}

void loop() {
  M5.update();
}