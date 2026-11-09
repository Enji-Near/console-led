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
//   offnormal <w> <w>    set the dial's off-normal contacts (ends a digit without waiting)
//   hook <wire> <wire>   set the hook switch contacts
//   channel <n>|scan     use this Wi-Fi channel / re-score channels around the current one
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
  esp_wifi_set_ps(WIFI_PS_NONE);               // no modem sleep: radio always ready (latency)
  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW init failed"); return; }
  esp_now_register_send_cb(onSent);
  addPeer(BCAST);
  addPeer(target);
  setChannel(prefs.getUChar("ch", START_CHANNEL));   // where Glorious-Hole answered last time
  // settleChannel() runs at the end of setup(), once the target MAC is known
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

// Neighbouring 2.4 GHz channels overlap, so Glorious-Hole on ch 6 still ACKs some frames sent on
// ch 5 or 7, just unreliably (retries, 20-70 ms). After finding it, score ch-2..ch+2 with 1-byte
// probes (WLED ignores them; the radio still ACKs) and keep the channel with the most ACKs.
// Score = ACKs out of n probes; ties broken by mean time to ACK (the real channel answers on the
// first air attempt, a neighbour needs the radio's own retries and is slower).
struct Score { int acks; uint32_t meanUs; };
static Score probeScore(int n) {
  static const uint8_t ping[1] = { 0 };
  Score sc = { 0, 0 };
  uint32_t total = 0;
  for (int i = 0; i < n; i++) {
    sendDone = false;
    const uint32_t t0 = micros();
    if (esp_now_send(target, ping, 1) != ESP_OK) continue;
    while (!sendDone && micros() - t0 < 50000) delayMicroseconds(50);
    if (sendDone && sendAcked) { sc.acks++; total += micros() - t0; }
  }
  sc.meanUs = sc.acks ? total / sc.acks : 0;
  return sc;
}

static void settleChannel(uint8_t around) {
  if (isBroadcast()) return;
  int best = around;
  Score bestSc = { -1, 0 };
  Serial.print("channel scores:");
  for (int c = max(1, around - 2); c <= min(13, around + 2); c++) {
    setChannel(c);
    const Score sc = probeScore(12);
    Serial.printf(" ch%d=%d/12@%.1fms", c, sc.acks, sc.meanUs / 1000.0f);
    if (sc.acks > bestSc.acks || (sc.acks == bestSc.acks && sc.acks > 0 && sc.meanUs < bestSc.meanUs)) { best = c; bestSc = sc; }
  }
  setChannel(best);
  if (prefs.getUChar("ch", 0) != best) prefs.putUChar("ch", best);
  Serial.printf(" -> ch %d\n", best);
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
  if (n) {
    Serial.printf("-> digit %d, ACKed on ch %d, try %d (%lu ms)\n", d, channel, n, millis() - t0);
    if (prefs.getUChar("ch", 0) != channel) prefs.putUChar("ch", channel);   // so a reboot starts here
    return;
  }
  // Nobody answering here: sweep every channel, twice, before giving up.
  const uint8_t was = channel;
  for (int pass = 0; pass < 2; pass++)
  for (uint8_t ch = 1; ch <= 13; ch++) {
    if (pass == 0 && ch == was) continue;
    setChannel(ch);
    if ((n = sendRetry(target, m, 4))) {
      Serial.printf("-> digit %d, Glorious-Hole moved: ch %d -> %d, ACKed (%lu ms)\n", d, was, ch, millis() - t0);
      settleChannel(ch);                       // the first channel that answers may be a neighbour
      return;
    }
  }
  setChannel(was);
  Serial.printf("-> digit %d, NOT ACKed on any channel: Glorious-Hole off or out of range\n", d);
}

// ---- Settings ---------------------------------------------------------------------------
static int pulseA = -1, pulseB = -1, hookA = -1, hookB = -1;   // wire indexes, -1 = not set
static int offA = -1, offB = -1;                                // dial off-normal contacts
static bool probing = false;

static void loadSettings() {
  prefs.begin("dialer", false);
  pulseA = prefs.getChar("pa", -1); pulseB = prefs.getChar("pb", -1);
  hookA  = prefs.getChar("ha", -1); hookB  = prefs.getChar("hb", -1);
  offA   = prefs.getChar("oa", -1); offB   = prefs.getChar("ob", -1);
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
  if (offA   >= 0) { pinMode(WIRES[offA].pin,   OUTPUT); digitalWrite(WIRES[offA].pin,   LOW); }
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
// digit N = N pulses, 0 = 10 pulses. We learn each contact's resting state instead of assuming it.
// End of digit: the off-normal contacts return to rest when the dial is home, right after the
// last pulse, so the digit is sent at once. Without an off-normal pair we must instead wait for
// a gap longer than any gap between pulses (DIGIT_GAP_MS), which costs that much latency.
static constexpr uint32_t DEBOUNCE_MS = 5, DIGIT_GAP_MS = 200, REST_LEARN_MS = 600;

struct Contact {                               // one debounced contact pair with a learned rest state
  bool state = false, raw = false, rest = false, restKnown = false;
  uint32_t rawSince = 0, stableSince = 0;
  // Returns true when the debounced state changed.
  bool update(bool r, uint32_t now) {
    if (r != raw) { raw = r; rawSince = now; }
    if (now - rawSince >= DEBOUNCE_MS && raw != state) { state = raw; stableSince = now; return true; }
    return false;
  }
  bool atRest() const { return restKnown && state == rest; }
};
static Contact pc, oc;                         // pulse, off-normal
static int pulses = 0;
static uint32_t lastPulseEnd = 0;              // when the pulse contacts last returned to rest

static void learnRest(Contact& c, const char* name, uint32_t now) {
  if (pulses == 0 && now - c.stableSince >= REST_LEARN_MS && (!c.restKnown || c.rest != c.state)) {
    c.rest = c.state; c.restKnown = true;
    Serial.printf("dial at rest: %s contacts %s\n", name, c.state ? "closed" : "open");
  }
}

static void finishDigit(const char* how, uint32_t now) {
  const int n = pulses;
  pulses = 0;
  if (n > 10) { Serial.printf("ignored %d pulses (more than 10)\n", n); return; }
  Serial.printf("dialed %d (%s, %lu ms after the last pulse)\n", n % 10, how, (unsigned long)(now - lastPulseEnd));
  sendDigit(n % 10);
}

static void dialTick() {
  if (pulseA < 0 || pulseB < 0) return;
  const uint32_t now = millis();
  const bool haveOff = offA >= 0 && offB >= 0;

  if (pc.update(digitalRead(WIRES[pulseB].pin) == LOW, now) && pc.restKnown) {
    if (pc.state != pc.rest) pulses++;
    else lastPulseEnd = now;
  }
  learnRest(pc, "pulse", now);
  if (haveOff) { oc.update(digitalRead(WIRES[offB].pin) == LOW, now); learnRest(oc, "off-normal", now); }

  if (pulses > 0 && pc.atRest()) {
    if (haveOff && oc.atRest()) finishDigit("off-normal", now);
    else if ((!haveOff || !oc.restKnown) && now - pc.stableSince >= DIGIT_GAP_MS) finishDigit("gap", now);
    else if (haveOff && now - pc.stableSince >= 1500) finishDigit("off-normal never returned; gap", now);
  } else if (pulses > 0 && !pc.atRest() && now - pc.stableSince >= 1000) {
    Serial.println("pulse contacts stuck away from rest; wrong pair?");
    pulses = 0; pc.restKnown = false;
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
  Serial.printf("  pulse pair: %s + %s\n  off-normal: %s + %s\n  hook pair:  %s + %s\n", wname(pulseA), wname(pulseB),
                wname(offA), wname(offB), wname(hookA), wname(hookB));
  Serial.print("  wires:");
  for (auto& w : WIRES) Serial.printf(" %s=GPIO%d", w.name, w.pin);
  Serial.println("\n  commands: 0-9 | target <mac>|broadcast | channel <n>|scan | blink [s] | probe | pulse <wire> <wire> | offnormal <wire> <wire> | hook <wire> <wire> | status");
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
    if (setPair(args, pulseA, pulseB, "pa", "pb")) { pc = Contact(); pulses = 0; setupPins(); status(); }
  } else if (cmd == "offnormal") {
    if (setPair(args, offA, offB, "oa", "ob")) { oc = Contact(); pulses = 0; setupPins(); status(); }
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
  } else if (cmd == "channel") {
    const int c = args.toInt();
    if (c < 1 || c > 13) { Serial.println("channel <1-13> | channel scan"); if (args != "scan") return; settleChannel(channel); return; }
    setChannel(c); prefs.putUChar("ch", c); status();
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
  settleChannel(channel);                      // ~0.3 s: confirm Glorious-Hole's exact channel
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
