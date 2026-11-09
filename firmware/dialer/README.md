# Dialer (proof of concept)

A rotary phone that tells Glorious-Hole which preset to play. Dial 1..9 -> preset 1..9; dial 0 ->
preset 10 (WLED has no preset 0, and 0 is 10 pulses on a rotary dial). Mapping lives in
`../glorious-hole/remote.json`.

**PoC shortcut.** Glorious-Hole stays on stock WLED 16. WLED already accepts ESP-NOW "WiZmote"
remote packets from a linked MAC and looks the button up in `/remote.json`, so Dialer sends
WiZmote packets (button = 200 + digit) broadcast on channel 6. No auth beyond the MAC check, no
reply, no lamp-os framing: the real console-led protocol replaces this after the spec.

## Run it
```bash
cd firmware/dialer && pio run -t upload --upload-port /dev/ttyUSB5
pio device monitor -p /dev/ttyUSB5          # type a digit + Enter to send it, `status` for help
tools/link-glorious-hole.sh <settings-PIN>  # once: ESP-NOW on, link Dialer's MAC, remote.json
```
Glorious-Hole must be on Wi-Fi channel 6 (it is: MonkeyCArtcade24). Dialer is fixed to channel 6.

## Phone wiring
Every phone wire goes to its own GPIO, nothing to GND (table `WIRES` in `src/main.cpp`):

| Dial | GPIO | Hook switch | GPIO |
|---|---|---|---|
| red | 32 | red | 13 |
| blue | 33 | black | 16 |
| white | 27 | orange | 17 |
| yellow | 14 | yellow | 18 |
| | | green | 19 |
| | | white | 23 |

Disconnect these wires from the phone's old network board first. GPIOs 4, 5, 21, 22, 25, 26
are kept free for audio (I2S) later.

Then in the serial monitor:
1. `probe`, then lift/replace the handset and wind the dial. It prints which wires connect.
2. The pair that flickers while the dial unwinds is the pulse pair: `pulse dial-x dial-y`.
3. A pair that changes with the handset: `hook hook-x hook-y`. (Hook is only logged for now.)
4. `probe` again to leave probe mode. Pairs are saved in flash.
