# Glorious-Hole firmware (WLED)

Stock **WLED 16.0.1** `esp32dev` + `audioreactive` + `moving_lines` (Moving Lines / Moving Spiral
from `Artcade/Glorious-Worm-Hole`, the build Glorious-Hole ran before) + **`dial_presets`**.
Effect IDs are identical to the previous build (checked name by name), so presets are untouched.

```bash
firmware/wled/build.sh                 # -> firmware/wled/build/firmware.bin (clean v16.0.1 worktree at ../WLED-16.0.1)
tools/ota-glorious-hole.sh <PIN>       # flash over Wi-Fi; keeps settings and presets
~/.platformio/penv/bin/python tools/measure-latency.py 30    # Dialer on USB: response time per digit
```

## dial_presets
Catches Dialer's ESP-NOW press, applies the mapped preset from a RAM copy (no flash reads), and
renders the next frame at once. Crossfades and playlists behave as before. Mapping: Config >
Usermods > DialPresets (digit 0..9 -> preset; default 0->10, 1..9->1..9). Info page shows the last
press timing. The RAM copy reloads when presets are saved or `presets.json` is uploaded, so edit
presets in the WLED UI as usual (the Info page shows "N presets in RAM").

## Measured 2026-11-09 (Dialer 10 ft away, Glorious-Hole pinned to the ch-6 access point)
30/30 under 30 ms; median 9.9 ms, worst 25.1 ms, from the digit leaving Dialer to the first frame
of the new preset going to the LEDs (radio 2-7 ms, apply ~1.5 ms, frame ~5 ms after arrival).
Stock WLED took ~100 ms (two flash reads + a frame wait). Not included: clocking 553 pixels out
(~17 ms, the first pixels change at its start) and the crossfade itself (it starts in that frame).

## Settings changed on Glorious-Hole
- ESP-NOW on; Dialer `48aff36a0898` linked as a remote.
- Wi-Fi pinned to BSSID `54:07:7D:86:14:90` (MonkeyCArtcade24 on **channel 6**). The SSID also has
  an access point on channel 1; roaming there moved ESP-NOW with it. If the pinned AP is down,
  WLED's own AP comes up on channel 6 too. Undo: clear the BSSID in Config > WiFi Setup.
- `MovingLines` settings restored after the first OTA dropped them (archSpacing 8, ledSpacing 2.4).
