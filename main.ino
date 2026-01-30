
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_camera.h"
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// ===== WiFi =====
const char* ssid = "ssid";
const char* password = "password";

// ===== Telegram =====
#define BOTtoken "BOTtoken"

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

// ===== ESP32-CAM AI-Thinker =====
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ===== Telegram binary callbacks =====
camera_fb_t *globalFb = nullptr;
size_t fbIndex = 0;

bool moreDataAvailable() {
  bool more = (globalFb && fbIndex < globalFb->len);
  Serial.printf("[CB] moreDataAvailable = %d (idx=%u / %u)\n",
                more, (unsigned)fbIndex, globalFb ? (unsigned)globalFb->len : 0);
  return more;
}

uint8_t getNextByte() {
  uint8_t b = globalFb->buf[fbIndex++];
  if (fbIndex % 256 == 0) {
    Serial.printf("[CB] getNextByte idx=%u\n", (unsigned)fbIndex);
  }
  return b;
}

uint8_t* getNextBuffer() {
  Serial.printf("[CB] getNextBuffer idx=%u\n", (unsigned)fbIndex);
  return &globalFb->buf[fbIndex];
}

int getNextBufferLen() {
  int remaining = globalFb->len - fbIndex;
  int chunk = remaining > 512 ? 512 : remaining;
  Serial.printf("[CB] getNextBufferLen=%d (remaining=%d)\n", chunk, remaining);
  fbIndex += chunk;
  return chunk;
}

// ===== Globals =====
long lastUpdate = 0;
long offset = 0;

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32-CAM Telegram BOT ===");

  // WiFi
  Serial.print("[WiFi] Connecting");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] Connected");
  Serial.print("[WiFi] IP: ");
  Serial.println(WiFi.localIP());

  client.setInsecure();
  Serial.println("[TLS] client.setInsecure()");

  // Camera
  Serial.println("[CAM] Initializing...");
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_hmirror(s, 1);   // 0 = выкл зеркалирование по горизонтали
    s->set_vflip(s, 0);    // 0 = без вертикального переворота
  }
  if (err != ESP_OK) {
    Serial.printf("[CAM] Init FAILED: 0x%x\n", err);
    ESP.restart();
  }
  Serial.println("[CAM] Ready");
  
}

// ===== LOOP =====
void loop() {
  if (millis() - lastUpdate > 60000) { // each minute
    Serial.println("[BOT] Checking updates...");
    int numNewMessages = bot.getUpdates(offset);
    Serial.printf("[BOT] Updates received: %d\n", numNewMessages);

    for (int i = 0; i < numNewMessages; i++) {
      String chat_id = bot.messages[i].chat_id;
      String text = bot.messages[i].text;

      Serial.printf("[BOT] Chat: %s | Text: %s\n",
                    chat_id.c_str(), text.c_str());

      if (text == "/photo") {
        Serial.println("[CMD] /photo");

        bot.sendMessage(chat_id, "📸 Waiting make a photo...", "");

        Serial.println("[CAM] Capturing frame...");
        globalFb = esp_camera_fb_get();
        if (!globalFb) {
          Serial.println("[CAM] Capture FAILED");
          bot.sendMessage(chat_id, "❌ Camera error", "");
          continue;
        }

        Serial.printf("[CAM] Frame size: %u bytes\n",
                      (unsigned)globalFb->len);

        fbIndex = 0;
        Serial.println("[BOT] Sending photo...");

        bot.sendPhotoByBinary(
          chat_id,
          "image/jpeg",
          globalFb->len,
          moreDataAvailable,
          getNextByte,
          getNextBuffer,
          getNextBufferLen
        );

        Serial.println("[BOT] Photo send finished");

        esp_camera_fb_return(globalFb);
        globalFb = nullptr;
        Serial.println("[CAM] Frame buffer returned");
      }

      offset = bot.messages[i].update_id + 1;
    }

    lastUpdate = millis();
  }

  delay(20);
}
