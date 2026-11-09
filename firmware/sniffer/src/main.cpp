// ESP-NOW sniffer for bench debugging: prints every ESP-NOW frame heard on channel 6.
//   cd firmware/sniffer && pio run -t upload --upload-port /dev/ttyUSB4 && pio device monitor -p /dev/ttyUSB4
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

struct Frame { uint8_t mac[6]; uint8_t len; uint8_t data[32]; uint32_t ms; };
static Frame ring[32];
static volatile uint8_t head = 0;
static uint8_t tail = 0;
static volatile uint32_t total = 0;

static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  Frame& f = ring[head % 32];
  memcpy(f.mac, mac, 6);
  f.len = len;
  memcpy(f.data, data, min(len, 32));
  f.ms = millis();
  head++;
  total++;
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  esp_now_init();
  esp_now_register_recv_cb(onRecv);
  Serial.printf("\nsniffer on ch 6, MAC %s\n", WiFi.macAddress().c_str());
}

void loop() {
  while (tail != head) {
    const Frame& f = ring[tail % 32];
    Serial.printf("%7lu %02x%02x%02x%02x%02x%02x len %2u:", (unsigned long)f.ms, f.mac[0], f.mac[1], f.mac[2], f.mac[3], f.mac[4], f.mac[5], f.len);
    for (int i = 0; i < min<int>(f.len, 16); i++) Serial.printf(" %02x", f.data[i]);
    Serial.println();
    tail++;
  }
  delay(2);
}
