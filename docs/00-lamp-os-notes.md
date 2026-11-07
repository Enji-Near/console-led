# lamp-os notes, and what we carry over from the old controller

Background for console-led. Written 2026-11-06 from a read of lamp-os v1.3.0 and Nick's earlier
WLED-Controller repo; moved here 2026-11-07. Facts with sources; the code in lamp-os wins ties.

lamp-os clone: `~/Documents/Enji_Nearing/_reference/lamp-os` (upstream
https://github.com/wearelamplit/lamp-os). Authoritative docs there: `docs/dev/networking.md`
(wire format), `docs/adrs/0001-esp-now-mesh-networking.md`, `docs/adrs/0002-ota-over-mesh.md`,
`docs/dev/lamp-social-api.md`, `docs/dev/architecture.md`.

## Licence
- **No LICENSE file** in lamp-os, so all rights are reserved by default.
- Until the authors add a licence or give permission: write our own code that interoperates, use
  their docs as the spec, and do not copy their source.

## Transport
- ESP-NOW broadcast to `FF:FF:FF:FF:FF:FF`, fixed **Wi-Fi channel 6**. No pairing, no
  association, no root node. A lamp joins by powering on and sending `MSG_HELLO`.
- **No routing.** Gossip relay only: a node re-broadcasts certain message types once, deduped
  per type by `(sourceMac, msgType, seq)` in fixed-size rings.
  - Relayed: `MSG_HELLO` (delayed by a random jitter and suppressed if enough copies are already
    heard), `MSG_CONTROL_OP`, wisp beacons (one hop, only when heard direct from the wisp).
  - **Not relayed, by design:** `MSG_EVENT` and `MSG_COMMAND` (nearby lamps only), overrides,
    `WISP_CLAIM`, `WISP_STATE`.
  - A lamp only relays types it parses. **Unknown message types are dropped, not relayed.**
- Frame header: `'L' 'M' version msgType seq(2 LE)`. Emits v0x05, parses range 0x04..0x05.
  Additive fields ride as TLVs that older receivers skip ("TLV-first" rule).
- ESP-NOW v1 frames are max 250 B; v2 up to 1470 B (lamp-os uses v2 for large frames).

## Identity and presence
- `MSG_HELLO`: every 5 s for the first 30 s after boot, then every 30 s. Pruned after 240 s of
  silence. Plaintext and relayed.
- Carries: source MAC, shade and base colour, firmware version, **name (0..32 chars)**, and TLVs
  including lamp variant (Standard, Snafu, Staff, Lioness, Loaf).
- `lampId` = the colon-hex MAC. A friendly name like "Lampsie McGuire" is the `name` field.
- => Detecting a named lamp only needs listening on channel 6. No lamp firmware change.

## Commanding lamps
- `MSG_COMMAND` (targeted) and `MSG_EVENT` (announce) carry an `ExpressionInvocation` JSON, e.g.
  `{"type":"glitchy","target":3,"colors":"ff0000000000ff00","parameters":{"durationMin":250}}`.
- Both carry an 8-byte `HMAC-SHA256(key, frame)[:8]` tag. The 32-byte key is baked into signed
  (beta/stable) builds. Keyed lamps drop untagged frames.
- => Making lamps react to us needs the key, or a new arrangement, from the authors.

## LEDs and behaviour
- Own compositor, not WLED. Two surfaces (shade, base), RGBW NeoPixels, effects called
  "expressions".
- Inputs are supported (Button, Touch state machines) but most lamps have none.
- Interaction is social: greetings shaped by per-peer "disposition" (Salty..Smitten), crowd-dim,
  rare group "Moments". API: `onArrival(PeerView)`, `forEachNearby`.

## App, wisp, OTA
- App: Flutter, talks BLE GATT to lamps with app-layer AES-GCM auth. No direct mesh role; reaches
  the wisp by proxy through a lamp.
- Wisp (infra node, ESP32-C6): one radio does Wi-Fi and ESP-NOW on the same channel, so it only
  joins routers found on channel 6. Optional wired Ethernet (W5500) keeps its feed off the radio.
- OTA: signed (ed25519) lamp-to-lamp gossip OTA, chunked, with re-request recovery and A/B slots
  (ADR 0002). A large engineering effort.
- Build: PlatformIO, pioarduino espressif32, Arduino framework, NimBLE, ArduinoJson 7.

## Carried over from the old WLED-Controller repo
Old repo: `~/Documents/Enji_Nearing/WLED-Controller` (port pieces in as needed).
- Hardware: ESP32 DevKit V1 (WROOM-32) on a PTSolns Proto-ESP V2.0, 8 LED outputs buffered to 5 V.
- Firmware: WLED 16.0.1 + usermods `custom_fx` (custom effects, slow-motion patch) and
  `wled_link` (ESP-NOW link, ADR-0007).
- `wled_link` protocol (`firmware/usermods/wled_link/link_protocol.h`): header
  `'W' 'L' ver type net src dst ttl boot seq`; 1-byte node ids from the profile; no relay
  (`ttl` reserved); no store-and-forward; events sent once, state re-beaconed every 2 s;
  duplicates dropped by `(src, boot, seq)`; no wall clocks. Input node hops channels to find
  light nodes. Light nodes use WLED's own ESP-NOW instance (`onEspNowMessage()` hook).
- Old Console (`controller-app/`): vanilla JS browser app talking HTTP/WebSocket JSON to WLED over
  Wi-Fi; faders, bumps, motion, games. Shows which WLED settings each effect actually uses.
- Lesson from Halloween: WLED searching for a lost venue network restarts its own AP and drops
  whoever is on it, so the profile now runs with no venue Wi-Fi at events.
