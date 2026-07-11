#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

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

#define MAX_IMG_SIZE (300 * 1024)                   // 300KB
#define MAX_CHUNKS   (MAX_IMG_SIZE / CHUNK_SIZE + 1) // ~1336
#define FRAME_TIMEOUT_MS 300                         // これだけ新chunkが来なければ強制送出/破棄
// ※DESCENDING時のsendIntervalMs=200msより余裕を持たせた値。
//   rocket_cam.cpp側の送信間隔を変えたらここも合わせて見直すこと。

// ==== ダブルバッファ (ping-pong) ====
// なぜ2面必要か:
//   sendTaskがSerial送信中でも、その間にonDataRecvは次フレームの受信を続けられる必要がある。
//   1面だと「送信中バッファに新フレームが書き込まれる」競合が起きるため、
//   フレーム完成のたびに書き込み先を切り替える。
struct FrameSlot {
  uint8_t* buffer = nullptr;
  uint8_t  receivedBitmap[(MAX_CHUNKS + 7) / 8] = {0};
  uint16_t currentImageId = 0xFFFF;
  uint16_t chunksReceived = 0;
  uint16_t totalChunksExpected = 0;
  uint32_t maxOffsetDetected = 0;
  uint32_t lastPacketTimeMs = 0;
  bool     active = false; // 現在このスロットが受信中か
};

FrameSlot slots[2];
volatile int writeSlotIdx = 0; // 現在パケットを書き込んでいるスロット
SemaphoreHandle_t slotMutex;   // writeSlotIdxの切り替え判断(onDataRecv/timeoutCheckTask間の競合防止)

struct SendJob {
  int slotIdx;
  uint32_t dataLen;
};
QueueHandle_t sendQueue;

// ==== 通信状況モニタ用カウンタ ====
// displayTaskが500ms毎に差分を取ってスループット等を算出する。
// onDataRecv/timeoutCheckTaskからのインクリメントのみで、displayTask以外は読まないので
// 厳密な排他は省略(表示用途であり多少のずれは許容)。
volatile uint32_t statsPacketsTotal   = 0; // 受信した有効長パケットの総数(重複含む)
volatile uint32_t statsBytesTotal     = 0; // 実際にバッファへ書き込んだユニークバイト数
volatile uint32_t statsFramesComplete = 0; // 全chunk揃って完成したフレーム数
volatile uint32_t statsFramesPartial  = 0; // タイムアウトで打ち切った(欠損あり)フレーム数
volatile uint32_t statsRawCallbacks   = 0; // onDataRecvが呼ばれた回数(len不一致の異常パケットも含む)

inline void setBit(uint8_t* bitmap, uint16_t idx) { bitmap[idx / 8] |= (1 << (idx % 8)); }
inline bool getBit(uint8_t* bitmap, uint16_t idx) { return bitmap[idx / 8] & (1 << (idx % 8)); }

void resetSlot(FrameSlot &slot) {
  memset(slot.receivedBitmap, 0, sizeof(slot.receivedBitmap));
  slot.currentImageId = 0xFFFF;
  slot.chunksReceived = 0;
  slot.totalChunksExpected = 0;
  slot.maxOffsetDetected = 0;
  slot.active = false;
}

// フレーム完成 or タイムアウト成立時の共通処理: 送信キューに投げてスロットを切り替える
// slotMutexを保持した状態で呼ぶこと
void flushSlotAndSwitch(int idx, bool complete) {
  FrameSlot &slot = slots[idx];
  if (slot.chunksReceived > 0) {
    SendJob job = { idx, slot.maxOffsetDetected };
    xQueueSend(sendQueue, &job, 0); // キュー満杯なら諦める(sendTaskが詰まってる場合の保険)
    if (complete) statsFramesComplete++;
    else          statsFramesPartial++;
  }
  slot.active = false;
  writeSlotIdx = 1 - idx;
}

// 💡 ESP-NOW受信コールバック
//    Espressif公式の注意事項通り、ここでは重い処理(Serial書き込み等)を一切しない。
//    バッファへのmemcpyとビットマップ更新のみ。
void onDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  statsRawCallbacks++;
  if (statsRawCallbacks <= 5) {
    // 最初の数回だけ詳細ログ。expected=sizeof(Packet)と合っているか確認用。
    Serial.printf("recv#%lu from %02X:%02X:%02X:%02X:%02X:%02X len=%d (expected=%d)\n",
      statsRawCallbacks, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], len, (int)sizeof(Packet));
  }
  if (len != sizeof(Packet)) return;
  Packet pkt;
  memcpy(&pkt, incomingData, sizeof(Packet));
  if (pkt.chunk_idx >= MAX_CHUNKS) return; // 異常データからバッファを守る

  statsPacketsTotal++;

  xSemaphoreTake(slotMutex, portMAX_DELAY);
  int idx = writeSlotIdx;
  FrameSlot &slot = slots[idx];

  if (!slot.active || pkt.image_id != slot.currentImageId) {
    // 新しいimage_idの到来 = 前のフレームが未完了でも打ち切って新規開始
    // (タイムアウトより前にここに来るケースは「前フレームが極端に酷いロスだった」場合)
    resetSlot(slot);
    slot.currentImageId = pkt.image_id;
    slot.totalChunksExpected = pkt.total_chunks;
    slot.active = true;
  }

  uint32_t offset = (uint32_t)pkt.chunk_idx * CHUNK_SIZE;
  if (offset + pkt.data_len <= MAX_IMG_SIZE) {
    if (!getBit(slot.receivedBitmap, pkt.chunk_idx)) {
      memcpy(slot.buffer + offset, pkt.payload, pkt.data_len);
      setBit(slot.receivedBitmap, pkt.chunk_idx);
      slot.chunksReceived++;
      statsBytesTotal += pkt.data_len;
      if (offset + pkt.data_len > slot.maxOffsetDetected) {
        slot.maxOffsetDetected = offset + pkt.data_len;
      }
    }
    slot.lastPacketTimeMs = millis();

    if (slot.chunksReceived >= slot.totalChunksExpected) {
      flushSlotAndSwitch(idx, true); // 全chunk揃った→即送出
    }
  }
  xSemaphoreGive(slotMutex);
}

// 💡 タイムアウト監視タスク
//    一定時間新しいchunkが来ずに止まっているフレームを、部分フレームのまま強制送出する。
//    (旧実装は1chunkでも欠けると永久に未完成のまま = そのフレームは黙って消えていた)
void timeoutCheckTask(void *pv) {
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(50));
    xSemaphoreTake(slotMutex, portMAX_DELAY);
    int idx = writeSlotIdx;
    FrameSlot &slot = slots[idx];
    if (slot.active && (millis() - slot.lastPacketTimeMs > FRAME_TIMEOUT_MS)) {
      flushSlotAndSwitch(idx, false);
    }
    xSemaphoreGive(slotMutex);
  }
}

// 💡 Serial送信タスク
//    onDataRecv(Wi-Fiタスク上で動く)から重い処理を追い出すためにキュー経由で分離。
//    ここでブロッキングしても、次フレームの受信は別スロットで継続できる。
void sendTask(void *pv) {
  SendJob job;
  while (true) {
    if (xQueueReceive(sendQueue, &job, portMAX_DELAY)) {
      if (job.dataLen == 0) continue;
      FrameSlot &slot = slots[job.slotIdx];
      Serial.write("IMG:");
      Serial.write((uint8_t*)&job.dataLen, 4);
      Serial.write(slot.buffer, job.dataLen);
      Serial.flush();
    }
  }
}

// 💡 通信状況インジケーター表示タスク
//    500ms毎にstats*カウンタの差分からスループット・パケットレート・FPSを算出して表示する。
//    直近500ms内にパケットが1つも来ていなければ赤字で「NO SIGNAL」にする。
void displayTask(void *pv) {
  uint32_t lastBytes = 0, lastPkts = 0, lastComplete = 0, lastPartial = 0;
  uint32_t lastTick = millis();

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(500));
    uint32_t now = millis();
    float intervalS = (now - lastTick) / 1000.0f;
    lastTick = now;
    if (intervalS <= 0) continue;

    uint32_t curBytes    = statsBytesTotal;
    uint32_t curPkts     = statsPacketsTotal;
    uint32_t curComplete = statsFramesComplete;
    uint32_t curPartial  = statsFramesPartial;

    uint32_t deltaPkts = curPkts - lastPkts;
    float kbps    = (curBytes - lastBytes) / 1024.0f / intervalS;
    float pktRate = deltaPkts / intervalS;
    float fps     = (curComplete - lastComplete) / intervalS;
    float partialRate = (curPartial - lastPartial) / intervalS;

    lastBytes = curBytes; lastPkts = curPkts;
    lastComplete = curComplete; lastPartial = curPartial;

    bool signalAlive = deltaPkts > 0;

    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(signalAlive ? TFT_GREEN : TFT_RED, TFT_BLACK);
    M5.Display.println(signalAlive ? "GROUND: RX OK" : "GROUND: NO SIGNAL");
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.printf("KB/s : %6.1f\n", kbps);
    M5.Display.printf("pkt/s: %6.1f\n", pktRate);
    M5.Display.printf("fps  : %5.1f\n", fps);
    M5.Display.printf("part : %5.1f/s\n", partialRate);
    M5.Display.printf("total: %lu ok\n", curComplete);
    M5.Display.printf("      %lu lost\n", curPartial);
    M5.Display.printf("raw: %lu\n", statsRawCallbacks);
  }
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 2000000;
  M5.begin(cfg);

  M5.Display.begin();
  M5.Display.setBrightness(120);
  M5.Display.clear(BLUE);
  M5.Display.setCursor(10, 10);
  M5.Display.setTextColor(WHITE);
  M5.Display.setTextSize(2);
  M5.Display.println("GROUND");

  for (int i = 0; i < 2; i++) {
    slots[i].buffer = (uint8_t*)heap_caps_malloc(MAX_IMG_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (slots[i].buffer == nullptr) {
      Serial.println("FATAL: PSRAM alloc failed for frame slot");
    }
    resetSlot(slots[i]);
  }

  slotMutex = xSemaphoreCreateMutex();
  sendQueue = xQueueCreate(4, sizeof(SendJob));

  WiFi.mode(WIFI_STA);
  // LRモードは外した。rocket_cam.cpp側が使っていない非対称設定だったのと、
  // 50-100mのレンジならLRのスループット低下の方がデメリットが大きいため。
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); // rocket_cam.cpp側とチャンネルを明示的に一致させる

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onDataRecv);
    Serial.println("ESP-NOW init OK, recv_cb registered");
  } else {
    Serial.println("FATAL: esp_now_init failed");
  }
  Serial.printf("WiFi STA MAC: %s\n", WiFi.macAddress().c_str());

  xTaskCreatePinnedToCore(sendTask, "sendTask", 8192, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(timeoutCheckTask, "timeoutCheck", 2048, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(displayTask, "displayTask", 4096, nullptr, 1, nullptr, 1);
}

void loop() {
  vTaskDelete(NULL);
}