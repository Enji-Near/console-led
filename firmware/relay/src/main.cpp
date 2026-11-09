// Relay (PoC courier node): sits between Dialer and Glorious-Hole and passes Dialer's presses on.
//
// - Listens for WiZmote-format packets (13 bytes, see firmware/dialer) from Dialer's MAC only.
// - Forwards each one unchanged (same seq, so WLED acts on it once) unicast to Glorious-Hole,
//   retrying until ACKed. Glorious-Hole must list this relay's MAC as a linked remote.
// - Lives on Glorious-Hole's Wi-Fi channel. If Glorious-Hole stops ACKing it sweeps channels
//   1..13 to find it again (Dialer finds the relay the same way, by ACK).
// - Finds Glorious-Hole's channel at boot and re-checks every 10 s with a 1-byte probe that WLED
//   ignores (not a WiZmote packet), so it is already on the right channel when a press arrives.
// - Forwards each Dialer press once (Dialer's retries repeat the seq; repeats are dropped here).
// - Blinks the onboard LED once per forwarded press: green light for "the relay heard Dialer".
//
// Serial (115200) logs every press. Commands: `status`.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>

static const uint8_t DIALER[6] = { 0x48, 0xAF, 0xF3, 0x6A, 0x08, 0x98 };
static const uint8_t GH[6]     = { 0x30, 0x76, 0xF5, 0xE8, 0xD4, 0x30 };   // Glorious-Hole STA MAC
static constexpr uint8_t START_CHANNEL = 6;
static constexpr uint8_t LED_PIN = 2;

static Preferences prefs;
static uint8_t channel = START_CHANNEL;
static volatile bool sendDone = false, sendAcked = false;

// Filled by the receive callback (Wi-Fi task), drained in loop().
static constexpr int RING = 8;
static uint8_t ring[RING][13];
static volatile uint8_t head = 0;
static uint8_t tail = 0;
static uint32_t forwarded = 0, failed = 0, ignored = 0;

static void onSent(const uint8_t*, esp_now_send_status_t st) { sendAcked = st == ESP_NOW_SEND_SUCCESS; sendDone = true; }

static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (memcmp(mac, DIALER, 6) || len != 13 || (data[0] != 0x81 && data[0] != 0x91)) { ignored++; return; }
  const uint8_t next = (head + 1) % RING;
  if (next == tail) return;                    // full: drop (Dialer retries until someone ACKs anyway)
  memcpy(ring[head], data, 13);
  head = next;
}

static void setChannel(uint8_t ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  channel = ch;
}

static void addPeer(const uint8_t* mac) {
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6);
  p.channel = 0;                               // follow the radio's current channel
  p.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&p);
}

static bool sendAck(const uint8_t* msg, size_t len = 13) {
  sendDone = false;
  if (esp_now_send(GH, msg, len) != ESP_OK) return false;
  const uint32_t t0 = millis();
  while (!sendDone && millis() - t0 < 50) delay(1);
  return sendDone && sendAcked;
}

static int sendRetry(const uint8_t* msg, int tries) {
  for (int i = 1; i <= tries; i++) {
    if (sendAck(msg)) return i;
    delay(10 + (esp_random() & 15));
  }
  return 0;
}

// Is Glorious-Hole answering on this channel? A 1-byte frame: WLED drops it, the radio still ACKs.
static bool probeHere(int tries) {
  static const uint8_t ping[1] = { 0 };
  for (int i = 0; i < tries; i++) { if (sendAck(ping, 1)) return true; delay(10 + (esp_random() & 15)); }
  return false;
}

static void findGH() {
  if (probeHere(15)) return;
  const uint8_t was = channel;
  for (int pass = 0; pass < 2; pass++)
  for (uint8_t ch = 1; ch <= 13; ch++) {
    setChannel(ch);
    if (probeHere(4)) {
      if (ch != was) { prefs.putUChar("ch", ch); Serial.printf("Glorious-Hole found on ch %d\n", ch); }
      return;
    }
  }
  setChannel(was);
  Serial.println("Glorious-Hole not answering on any channel");
}

static uint8_t lastSeq[4];
static bool haveLast = false;

static void forward(const uint8_t* msg) {
  if (haveLast && !memcmp(lastSeq, msg + 1, 4)) return;   // Dialer retry of a press already sent
  memcpy(lastSeq, msg + 1, 4); haveLast = true;
  const int digit = msg[6] - 200;
  const uint32_t t0 = millis();
  int n = sendRetry(msg, 30);
  if (n) {
    forwarded++;
    Serial.printf("digit %d -> Glorious-Hole on ch %d, try %d (%lu ms)\n", digit, channel, n, millis() - t0);
  } else {
    const uint8_t was = channel;
    for (uint8_t ch = 1; ch <= 13 && !n; ch++) {
      if (ch == was) continue;
      setChannel(ch);
      n = sendRetry(msg, 4);
    }
    if (n) {
      forwarded++;
      prefs.putUChar("ch", channel);
      Serial.printf("digit %d -> Glorious-Hole moved ch %d -> %d (%lu ms)\n", digit, was, channel, millis() - t0);
    } else {
      failed++;
      setChannel(was);
      Serial.printf("digit %d -> Glorious-Hole NOT reached on any channel\n", digit);
    }
  }
  if (n) { digitalWrite(LED_PIN, HIGH); delay(150); digitalWrite(LED_PIN, LOW); }
}

static void status() {
  Serial.printf("Relay  MAC %s  channel %d  forwarded %lu  failed %lu  ignored %lu\n",
                WiFi.macAddress().c_str(), channel, (unsigned long)forwarded, (unsigned long)failed, (unsigned long)ignored);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  prefs.begin("relay", false);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_now_init();
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onRecv);
  addPeer(GH);
  setChannel(prefs.getUChar("ch", START_CHANNEL));
  Serial.println("\n=== Relay PoC ===");
  status();
  for (int i = 0; i < 3; i++) { digitalWrite(LED_PIN, HIGH); delay(100); digitalWrite(LED_PIN, LOW); delay(100); }
  findGH();
  status();
}

static uint32_t lastCheck = 0;

static String rx;

void loop() {
  while (tail != head) { uint8_t msg[13]; memcpy(msg, ring[tail], 13); tail = (tail + 1) % RING; forward(msg); lastCheck = millis(); }
  if (millis() - lastCheck > 10000) { lastCheck = millis(); findGH(); }
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\n' || c == '\r') { rx.trim(); if (rx == "status") status(); rx = ""; }
    else if (rx.length() < 32) rx += c;
  }
  delay(1);
}
