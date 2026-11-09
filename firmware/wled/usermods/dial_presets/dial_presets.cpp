// dial_presets - fast path from Dialer (rotary phone) to WLED presets. Written for WLED 16.0.1.
//
// Stock WLED handles a remote button by reading /remote.json and then /presets.json from flash
// and waiting for the strip to finish a frame: about 80 ms on Glorious-Hole. This usermod:
//   1. catches Dialer's ESP-NOW packet in onEspNowMessage() (WiZmote format, button 200 + digit,
//      13 bytes, see firmware/dialer) from a MAC on WLED's linked-remote list,
//   2. applies the mapped preset in the next loop() from a copy kept in RAM (no file reads), the
//      same way WLED's handlePresets() does, so transitions (crossfades) and playlists still work,
//   3. asks the strip to render the next frame right away.
// The RAM copy is reloaded whenever presets are saved or presets.json is uploaded.
//
// Config -> Usermods -> DialPresets: enabled, preset for each digit 0..9 (0 = ignore that digit).
// Info page: last digit, radio-to-applied time.
#include "wled.h"

class DialPresetsUsermod : public Usermod {
  static constexpr uint8_t BUTTON_BASE = 200;
  static constexpr uint8_t DIGITS = 10;

  bool    enabled = true;
  uint8_t digitPreset[DIGITS] = { 10, 1, 2, 3, 4, 5, 6, 7, 8, 9 };

  // Preset JSON per digit, serialized, in RAM. nullptr = not loaded / preset missing.
  char*         cache[DIGITS] = {};
  unsigned long cachedAt = 0;          // presetsModifiedTime the cache was built from
  bool          cacheDirty = true;

  // Written by onEspNowMessage (ESP-NOW receive task), read in loop().
  volatile int8_t   pending = -1;
  volatile uint32_t pendingAtUs = 0;
  uint32_t          lastSeq = 0;
  bool              haveSeq = false;

  // Stats for the Info page and /json/info "dial" (tools/measure-latency.py reads them).
  int8_t   lastDigit = -1;
  uint32_t lastApplyUs = 0, worstApplyUs = 0, applied = 0, fromFile = 0;
  uint32_t lastFrameUs = 0, worstFrameUs = 0;   // radio arrival -> first frame drawn with the new preset
  uint32_t rxAtUs = 0;
  bool     waitFrame = false;

  bool isLinked(const uint8_t* mac) const {
    char src[13];
    snprintf(src, sizeof(src), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    for (const auto& m : linked_remotes) if (strcmp(src, m.data()) == 0) return true;
    return false;
  }

  void freeCache() {
    for (auto& c : cache) { if (c) free(c); c = nullptr; }
  }

  // Reads each mapped preset from presets.json once and keeps it as a string. Not time critical.
  void loadCache() {
    if (!requestJSONBufferLock(JSON_LOCK_PRESET_LOAD)) return;   // busy: try again next loop
    freeCache();
    for (int d = 0; d < DIGITS; d++) {
      const uint8_t id = digitPreset[d];
      if (!id) continue;
      if (!readObjectFromFileUsingId(getPresetsFileName(), id, pDoc)) continue;
      JsonObject o = pDoc->as<JsonObject>();
      if (o.isNull() || !o["win"].isNull()) continue;            // HTTP-API presets: leave to WLED
      const size_t n = measureJson(*pDoc) + 1;
      cache[d] = static_cast<char*>(malloc(n));
      if (cache[d]) serializeJson(*pDoc, cache[d], n);
    }
    releaseJSONBufferLock();
    cachedAt = presetsModifiedTime;
    cacheDirty = false;
  }

  // Mirrors handlePresets() in wled00/presets.cpp, minus the file read.
  void apply(int d) {
    const uint8_t id = digitPreset[d];
    if (!id) return;
    if (!cache[d] || !requestJSONBufferLock(JSON_LOCK_PRESET_LOAD)) {
      applyPreset(id, CALL_MODE_BUTTON_PRESET);                 // fallback: WLED's normal (file) path
      fromFile++;
      return;
    }
    unloadPlaylist();                                           // as applyPreset() does
    deserializeJson(*pDoc, cache[d]);
    JsonObject fdo = pDoc->as<JsonObject>();
    fdo.remove("ps");                                           // no recursive preset loads
    deserializeState(fdo, CALL_MODE_NO_NOTIFY, id);             // transition + playlist handled here
    currentPreset = id;
    releaseJSONBufferLock();
    notify(CALL_MODE_BUTTON_PRESET);
    stateUpdated(CALL_MODE_BUTTON_PRESET);
    updateInterfaces(CALL_MODE_BUTTON_PRESET);
    strip.trigger();                                            // render now, not at the next frame slot
  }

 public:
  void setup() override {}

  void loop() override {
    if (!enabled) { pending = -1; return; }
    const int8_t d = pending;
    if (d >= 0) {
      pending = -1;
      rxAtUs = pendingAtUs;
      apply(d);
      waitFrame = true;
      lastDigit = d;
      lastApplyUs = micros() - rxAtUs;
      if (lastApplyUs > worstApplyUs) worstApplyUs = lastApplyUs;
      applied++;
      return;                                                   // keep this loop pass short
    }
    if (cacheDirty || cachedAt != presetsModifiedTime) loadCache();
  }

  // Called by strip.show() just before the frame goes out to the LEDs.
  void handleOverlayDraw() override {
    if (!waitFrame) return;
    waitFrame = false;
    lastFrameUs = micros() - rxAtUs;
    if (lastFrameUs > worstFrameUs) worstFrameUs = lastFrameUs;
  }

  // Runs on the ESP-NOW receive task: decide and record only, never touch WLED state here.
  bool onEspNowMessage(uint8_t* sender, uint8_t* payload, uint8_t len) override {
    if (!enabled || len != 13 || (payload[0] != 0x81 && payload[0] != 0x91)) return false;
    const uint8_t button = payload[6];
    if (button < BUTTON_BASE || button >= BUTTON_BASE + DIGITS) return false;   // other remotes: WLED's
    if (!isLinked(sender)) return false;                        // WLED logs and drops it
    const uint32_t seq = payload[1] | (payload[2] << 8) | (payload[3] << 16) | ((uint32_t)payload[4] << 24);
    if (haveSeq && seq == lastSeq) return true;                 // Dialer retry of the same press
    lastSeq = seq; haveSeq = true;
    pendingAtUs = micros();
    pending = button - BUTTON_BASE;
    return true;
  }

  void addToJsonInfo(JsonObject& root) override {
    JsonObject user = root["u"];
    if (user.isNull()) user = root.createNestedObject("u");
    JsonArray a = user.createNestedArray(F("Dialer"));
    if (!enabled) { a.add(F("disabled")); return; }
    int cached = 0;
    for (auto c : cache) cached += c != nullptr;
    char buf[112];
    if (lastDigit < 0) snprintf(buf, sizeof(buf), "no digit yet; %d presets in RAM", cached);
    else snprintf(buf, sizeof(buf), "last %d -> preset %u: applied %.1f ms, on LEDs %.1f ms (worst %.1f); %lu presses",
                  lastDigit, digitPreset[lastDigit], lastApplyUs / 1000.0f, lastFrameUs / 1000.0f,
                  worstFrameUs / 1000.0f, (unsigned long)applied);
    a.add(buf);
    JsonObject n = root.createNestedObject(F("dial"));
    n[F("digit")] = lastDigit;
    n[F("presses")] = applied;
    n[F("fromFile")] = fromFile;
    n[F("cached")] = cached;
    n[F("applyUs")] = lastApplyUs;
    n[F("frameUs")] = lastFrameUs;
  }

  void addToConfig(JsonObject& root) override {
    JsonObject top = root.createNestedObject(F("DialPresets"));
    top[F("enabled")] = enabled;
    JsonArray p = top.createNestedArray(F("digitPreset"));
    for (uint8_t v : digitPreset) p.add(v);
  }

  bool readFromConfig(JsonObject& root) override {
    JsonObject top = root[F("DialPresets")];
    bool complete = !top.isNull();
    complete &= getJsonValue(top[F("enabled")], enabled, true);
    JsonArray p = top[F("digitPreset")];
    static const uint8_t def[DIGITS] = { 10, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    for (int i = 0; i < DIGITS; i++) digitPreset[i] = (!p.isNull() && i < (int)p.size()) ? (uint8_t)(p[i] | 0) : def[i];
    complete &= !p.isNull();
    cacheDirty = true;
    return complete;
  }

  void appendConfigData() override {
    oappend(F("addInfo('DialPresets:digitPreset[]',1,'preset for digit 0,1..9 (0 = ignore)');"));
  }

  uint16_t getId() override { return USERMOD_ID_UNSPECIFIED; }
};

static DialPresetsUsermod dial_presets_usermod;
REGISTER_USERMOD(dial_presets_usermod);
