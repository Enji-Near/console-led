// Dialer proof of concept: a rotary phone that tells Glorious-Hole which preset to play.
//
// How it talks (PoC shortcut, not the final console-led protocol):
//   Glorious-Hole runs stock WLED 16, which already accepts ESP-NOW "WiZmote" remote packets from
//   a linked sender MAC and looks the button code up in /remote.json. Dialer sends a WiZmote-format
//   packet with button = 200 + digit, unicast to Glorious-Hole's MAC (set with `target`), finding
//   its Wi-Fi channel by ACK (see Radio). firmware/glorious-hole/remote.json maps 200..209 to presets.
//
// Wiring: every phone wire goes to its own GPIO (table WIRES below); no wire goes to GND. We
// don't know yet which wires form which switch, so the firmware can drive any wire LOW as a
// "virtual ground" and read the others with pull-ups:
//   probe mode   scans every wire against every other and prints which wires are connected,
//                each time that changes. Lift the handset, wind the dial, and watch.
//   normal mode  uses the two pairs you pick with the `pulse` and `hook` commands (saved in flash).
//
// Serial commands (115200, end with Enter):
//   0..9                 send that digit, no phone needed
//   target <mac>         send to this MAC (Glorious-Hole's) with ACK; `target broadcast` = no ACK
//   probe                toggle probe mode
//   pulse <wire> <wire>  set the dial's pulse contacts, e.g. `pulse dial-red dial-blue`
//   hook <wire> <wire>   set the hook switch contacts
//   blink [seconds]      blink the onboard LED (GPIO 2) to find this board; default 30 s
//   status               show settings
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>

// ---- Wires ------------------------------------------------------------------------------
// GPIOs 21, 22, 25, 26, 4, 5 stay free for the audio (I2S) work later.
struct Wire { const char* name; uint8_t pin; };
static const Wire WIRES[] = {
  { "dial-red",    32 }, { "dial-blue",   33 }, { "dial-white",  27 }, { "dial-yellow", 14 },
  { "hook-red",    13 }, { "hook-black",  16 }, { "hook-orange", 17 }, { "hook-yellow", 18 },
  { "hook-green",  19 }, { "hook-white",  23 },
};
static constexpr int NWIRES = sizeof(WIRES) / sizeof(WIRES[0]);

static int wireByName(const String& n) {
  for (int i = 0; i < NWIRES; i++) if (n.equalsIgnoreCase(WIRES[i].name)) return i;
  return -1;
}

// ---- Radio ------------------------------------------------------------------------------
// Glorious-Hole's ESP-NOW listens on whatever channel its Wi-Fi router uses, and the venue SSID
// has several access points on different channels (seen: 6 and 1), so it can move after a
// reboot or a roam. Unicast gets a MAC-level ACK, so Dialer finds it: send on the last good
// channel; no ACK -> try channels 1..13 until one ACKs, and remember it.
static constexpr uint8_t START_CHANNEL = 6;    // lamp-os mesh channel; where we look first
static constexpr uint8_t BUTTON_BASE = 200;    // digit d -> WiZmote button 200 + d
static const uint8_t BCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint32_t seq;
static uint8_t target[6];                      // Glorious-Hole's MAC; all FF = broadcast (no ACK)
static uint8_t channel = START_CHANNEL;
static volatile bool sendDone = false, sendAcked = false;

static void onSent(const uint8_t*, esp_now_send_status_t st) {
  sendAcked = st == ESP_NOW_SEND_SUCCESS;
  sendDone = true;
}

static bool isBroadcast() { return !memcmp(target, BCAST, 6); }

static void addPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6);
  p.channel = 0;                               // 0 = whatever channel the radio is on now
  p.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&p);
}

static Preferences prefs;

static void setChannel(uint8_t ch) {
  esp_wifi_set_promiscuous(true);              // channel can only be forced like this when not associated
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  channel = ch;
}

// WLED's WiZmote packet (wled00/remote.cpp): exactly 13 bytes or WLED ignores it.
struct __attribute__((packed)) WizMsg {
  uint8_t program;   // 0x81 = any button but ON
  uint8_t seq[4];    // LSB first; WLED drops a repeat of the last seq
  uint8_t dt1;       // 0x20
  uint8_t button;
  uint8_t dt2;       // 0x01
  uint8_t battery;   // 0..100
  uint8_t pad[4];
};
static_assert(sizeof(WizMsg) == 13, "WLED needs 13 bytes");

static void radioBegin() {
  WiFi.persistent(false);                      // never save Wi-Fi settings; this board joins no router
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW init failed"); return; }
  esp_now_register_send_cb(onSent);
  addPeer(BCAST);
  addPeer(target);
  setChannel(prefs.getUChar("ch", START_CHANNEL));   // where Glorious-Hole answered last time
  seq = esp_random();                          // a reboot must not reuse WLED's last seen seq
}

// One unicast send; true if the receiver's radio ACKed (the driver already retried it).
static bool sendAck(const uint8_t* to, const WizMsg& m) {
  sendDone = false;
  if (esp_now_send(to, (const uint8_t*)&m, sizeof(m)) != ESP_OK) return false;
  const uint32_t t0 = millis();
  while (!sendDone && millis() - t0 < 50) delay(1);
  return sendDone && sendAcked;
}

// Retry the same packet (same seq, so WLED acts on it once) until it is ACKed. The link to
// Glorious-Hole (through walls) sometimes needs 10+ tries, so one try is not enough.
static int sendRetry(const uint8_t* to, const WizMsg& m, int tries) {
  for (int i = 1; i <= tries; i++) {
    if (sendAck(to, m)) return i;
    delay(10 + (esp_random() & 15));
  }
  return 0;
}

static void sendDigit(int d) {
  WizMsg m = {};
  m.program = 0x81;
  ++seq;
  for (int i = 0; i < 4; i++) m.seq[i] = seq >> (8 * i);
  m.dt1 = 0x20; m.button = BUTTON_BASE + d; m.dt2 = 0x01; m.battery = 100;
  const uint32_t t0 = millis();

  if (isBroadcast()) {                         // no ACK: three copies, WLED drops repeats by seq
    for (int i = 0; i < 3; i++) { esp_now_send(BCAST, (const uint8_t*)&m, sizeof(m)); delay(8); }
    Serial.printf("-> digit %d (button %d), broadcast x3 on ch %d\n", d, m.button, channel);
    return;
  }
  int n = sendRetry(target, m, 30);            // ~1 s: a weak link needs patience; G-H rarely changes channel
  if (n) { Serial.printf("-> digit %d, ACKed on ch %d, try %d (%lu ms)\n", d, channel, n, millis() - t0); return; }
  // Nobody answering here: sweep every channel, twice, before giving up.
  const uint8_t was = channel;
  for (int pass = 0; pass < 2; pass++)
  for (uint8_t ch = 1; ch <= 13; ch++) {
    if (pass == 0 && ch == was) continue;
    setChannel(ch);
    if ((n = sendRetry(target, m, 4))) {
      prefs.putUChar("ch", ch);
      Serial.printf("-> digit %d, Glorious-Hole moved: ch %d -> %d, ACKed (%lu ms)\n", d, was, ch, millis() - t0);
      return;
    }
  }
  setChannel(was);
  Serial.printf("-> digit %d, NOT ACKed on any channel: Glorious-Hole off or out of range\n", d);
}

// ---- Settings ---------------------------------------------------------------------------
static int pulseA = -1, pulseB = -1, hookA = -1, hookB = -1;   // wire indexes, -1 = not set
static bool probing = false;

static void loadSettings() {
  prefs.begin("dialer", false);
  pulseA = prefs.getChar("pa", -1); pulseB = prefs.getChar("pb", -1);
  hookA  = prefs.getChar("ha", -1); hookB  = prefs.getChar("hb", -1);
  memset(target, 0xFF, 6);
  prefs.getBytes("tgt", target, 6);
  prefs.remove("via");                         // left over from the removed relay experiment
}

static const char* wname(int i) { return i >= 0 ? WIRES[i].name : "-"; }

// Normal mode: one wire of each pair driven LOW, the other read with a pull-up, the rest idle.
static void setupPins() {
  for (auto& w : WIRES) pinMode(w.pin, INPUT_PULLUP);
  if (probing) return;
  if (pulseA >= 0) { pinMode(WIRES[pulseA].pin, OUTPUT); digitalWrite(WIRES[pulseA].pin, LOW); }
  if (hookA  >= 0) { pinMode(WIRES[hookA].pin,  OUTPUT); digitalWrite(WIRES[hookA].pin,  LOW); }
}

// ---- Probe: which wires are connected to which ------------------------------------------
static uint16_t probeMap[NWIRES], probeShown[NWIRES], probeLast[NWIRES];
static uint32_t probeSince = 0;

static void probeScan(uint16_t* out) {
  for (int i = 0; i < NWIRES; i++) {
    for (auto& w : WIRES) pinMode(w.pin, INPUT_PULLUP);
    pinMode(WIRES[i].pin, OUTPUT);
    digitalWrite(WIRES[i].pin, LOW);
    delayMicroseconds(30);
    out[i] = 0;
    for (int j = 0; j < NWIRES; j++) if (j != i && digitalRead(WIRES[j].pin) == LOW) out[i] |= 1 << j;
  }
  for (auto& w : WIRES) pinMode(w.pin, INPUT_PULLUP);
}

static void probePrint() {
  Serial.printf("[%7lu ms] connected:", (unsigned long)millis());
  bool any = false;
  for (int i = 0; i < NWIRES; i++)
    for (int j = i + 1; j < NWIRES; j++)
      if (probeShown[i] & (1 << j)) { Serial.printf("  %s+%s", WIRES[i].name, WIRES[j].name); any = true; }
  Serial.println(any ? "" : "  (nothing)");
}

static void probeTick() {
  probeScan(probeMap);
  const uint32_t now = millis();
  if (memcmp(probeMap, probeLast, sizeof(probeMap))) { memcpy(probeLast, probeMap, sizeof(probeMap)); probeSince = now; }
  // Print once a new pattern has been stable for 15 ms, so contact bounce doesn't flood the log.
  if (now - probeSince >= 15 && memcmp(probeLast, probeShown, sizeof(probeMap))) {
    memcpy(probeShown, probeLast, sizeof(probeMap));
    probePrint();
  }
}

// ---- Dial: count pulses on the pulse contacts --------------------------------------------
// The dial opens (or closes) its pulse contacts once per count as it unwinds, 10 times a second:
// digit N = N pulses, 0 = 10 pulses. We learn the resting state instead of assuming it.
static constexpr uint32_t DEBOUNCE_MS = 8, DIGIT_GAP_MS = 200, REST_LEARN_MS = 600;
static bool pState, pRaw, pRest = false, pRestKnown = false;
static uint32_t pRawSince = 0, pStableSince = 0;
static int pulses = 0;

static void dialTick() {
  if (pulseA < 0 || pulseB < 0) return;
  const bool raw = digitalRead(WIRES[pulseB].pin) == LOW;    // true = contacts closed
  const uint32_t now = millis();
  if (raw != pRaw) { pRaw = raw; pRawSince = now; }
  if (now - pRawSince >= DEBOUNCE_MS && pRaw != pState) {
    pState = pRaw;
    pStableSince = now;
    if (pRestKnown && pState != pRest) pulses++;
  }
  const uint32_t held = now - pStableSince;
  if (!pRestKnown || (pulses == 0 && held >= REST_LEARN_MS)) {
    if (held >= REST_LEARN_MS) {
      if (!pRestKnown || pRest != pState) Serial.printf("dial at rest: pulse contacts %s\n", pState ? "closed" : "open");
      pRest = pState; pRestKnown = true;
    }
  }
  if (pulses > 0 && pState == pRest && held >= DIGIT_GAP_MS) {
    const int n = pulses;
    pulses = 0;
    if (n <= 10) sendDigit(n % 10);
    else Serial.printf("ignored %d pulses (more than 10)\n", n);
  } else if (pulses > 0 && pState != pRest && held >= 1000) {
    Serial.println("pulse contacts stuck away from rest; wrong pair?");
    pulses = 0; pRestKnown = false;
  }
}

// ---- Hook switch: just report it for now ------------------------------------------------
static bool hState, hRaw;
static uint32_t hRawSince = 0;
static bool hKnown = false;

static void hookTick() {
  if (hookA < 0 || hookB < 0) return;
  const bool raw = digitalRead(WIRES[hookB].pin) == LOW;
  if (raw != hRaw) { hRaw = raw; hRawSince = millis(); }
  if (millis() - hRawSince >= 30 && (!hKnown || hRaw != hState)) {
    hState = hRaw; hKnown = true;
    Serial.printf("hook contacts %s\n", hState ? "closed" : "open");
  }
}

// ---- Onboard LED: `blink` finds this board among several on the bench -------------------
static constexpr uint8_t LED_PIN = 2;
static uint32_t blinkUntil = 0;

static void blinkTick() {
  if (!blinkUntil) return;
  if ((int32_t)(millis() - blinkUntil) >= 0) { blinkUntil = 0; digitalWrite(LED_PIN, LOW); return; }
  digitalWrite(LED_PIN, (millis() / 200) & 1);
}

// ---- Serial commands --------------------------------------------------------------------
static String macStr(const uint8_t* m) {
  char b[13]; snprintf(b, sizeof(b), "%02x%02x%02x%02x%02x%02x", m[0], m[1], m[2], m[3], m[4], m[5]); return b;
}

static bool parseMac(String s, uint8_t* out) {
  s.replace(":", ""); s.trim();
  if (s.length() != 12) return false;
  for (int i = 0; i < 6; i++) out[i] = strtoul(s.substring(2 * i, 2 * i + 2).c_str(), nullptr, 16);
  return true;
}
static void status() {
  Serial.printf("Dialer  MAC %s  channel %d  mode %s\n", WiFi.macAddress().c_str(), channel, probing ? "PROBE" : "normal");
  Serial.printf("  target: %s\n", isBroadcast() ? "broadcast" : macStr(target).c_str());
  Serial.printf("  pulse pair: %s + %s\n  hook pair:  %s + %s\n", wname(pulseA), wname(pulseB), wname(hookA), wname(hookB));
  Serial.print("  wires:");
  for (auto& w : WIRES) Serial.printf(" %s=GPIO%d", w.name, w.pin);
  Serial.println("\n  commands: 0-9 | target <mac>|broadcast | blink [s] | probe | pulse <wire> <wire> | hook <wire> <wire> | status");
}

static bool setPair(const String& args, int& a, int& b, const char* keyA, const char* keyB) {
  const int sp = args.indexOf(' ');
  const int x = wireByName(args.substring(0, sp)), y = sp > 0 ? wireByName(args.substring(sp + 1)) : -1;
  if (x < 0 || y < 0 || x == y) { Serial.println("need two different wire names, see `status`"); return false; }
  a = x; b = y;
  prefs.putChar(keyA, a); prefs.putChar(keyB, b);
  return true;
}

static void command(String line) {
  line.trim();
  if (line.isEmpty()) return;
  if (line.length() == 1 && isDigit(line[0])) { sendDigit(line[0] - '0'); return; }
  const int sp = line.indexOf(' ');
  const String cmd = sp < 0 ? line : line.substring(0, sp);
  String args = sp < 0 ? "" : line.substring(sp + 1);
  args.trim();
  if (cmd == "probe") {
    probing = !probing;
    memset(probeShown, 0xFF, sizeof(probeShown));    // force the first print
    Serial.println(probing ? "PROBE on: lift the handset, wind the dial, watch what connects. `probe` again to stop."
                           : "PROBE off");
    setupPins();
  } else if (cmd == "pulse") {
    if (setPair(args, pulseA, pulseB, "pa", "pb")) { pRestKnown = false; pulses = 0; setupPins(); status(); }
  } else if (cmd == "hook") {
    if (setPair(args, hookA, hookB, "ha", "hb")) { hKnown = false; setupPins(); status(); }
  } else if (cmd == "target") {
    uint8_t m[6];
    if (args == "broadcast") memset(m, 0xFF, 6);
    else if (!parseMac(args, m)) { Serial.println("target <12 hex digits MAC> | target broadcast"); return; }
    memcpy(target, m, 6); prefs.putBytes("tgt", target, 6); addPeer(target); status();
  } else if (cmd == "blink") {
    const long secs = args.isEmpty() ? 30 : args.toInt();
    pinMode(LED_PIN, OUTPUT);
    blinkUntil = secs > 0 ? millis() + secs * 1000UL : 0;
    if (!blinkUntil) digitalWrite(LED_PIN, LOW);
    Serial.printf("blinking the onboard LED for %ld s\n", secs);
  } else if (cmd == "status") {
    status();
  } else {
    Serial.printf("unknown: %s\n", line.c_str());
  }
}

static String rxLine;

void setup() {
  Serial.begin(115200);
  delay(200);
  loadSettings();
  radioBegin();
  setupPins();
  Serial.println("\n=== Dialer PoC ===");
  status();
}

void loop() {
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\n' || c == '\r') { command(rxLine); rxLine = ""; }
    else if (rxLine.length() < 64) rxLine += c;
  }
  blinkTick();
  if (probing) { probeTick(); delay(2); return; }
  dialTick();
  hookTick();
  delay(1);
}
