#!/usr/bin/env python3
"""Measure Dialer -> Glorious-Hole response time.

Sends digits to Dialer over USB serial (as if dialed), then reads both ends' own timers:
  radio  Dialer: send until Glorious-Hole's radio ACKed (ms resolution; one-way is a bit less)
  apply  Glorious-Hole (dial_presets usermod): radio arrival -> preset applied
  LEDs   Glorious-Hole: radio arrival -> first frame with the new preset handed to the LEDs
Response = radio + LEDs. Not included: the dial itself (digit-end detection), and the ~17 ms it
takes to clock 553 pixels out (the first pixels change at the start of that). With a crossfade
preset, the first new frame is the start of the fade.

  tools/measure-latency.py [rounds] [--port /dev/ttyUSB0] [--ip 192.168.0.15]
Needs pyserial (the PlatformIO Python has it: ~/.platformio/penv/bin/python).
"""
import argparse, json, re, statistics, time, urllib.request
import serial

def drain(port, secs):
    """Read what is waiting for `secs` seconds. Only ever read in_waiting bytes: big read()
    calls returned replayed old data with pyserial on Python 3.14 here."""
    out, end = b"", time.time() + secs
    while time.time() < end:
        n = port.in_waiting
        if n: out += port.read(n)
        time.sleep(0.01)
    return out

def dial_stats(ip):
    for attempt in range(3):                             # Wi-Fi to the laptop hiccups now and then
        try:
            with urllib.request.urlopen(f"http://{ip}/json/info", timeout=3) as r:
                return json.load(r).get("dial")
        except OSError:
            if attempt == 2: raise
            time.sleep(0.5)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rounds", nargs="?", type=int, default=20)
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--ip", default="192.168.0.15")
    a = ap.parse_args()

    if dial_stats(a.ip) is None:
        raise SystemExit("Glorious-Hole has no dial_presets stats: is the custom WLED flashed?")
    dl = serial.Serial(a.port, 115200, timeout=0.05)
    t_end = time.time() + 10                             # opening the port may reset Dialer: wait for it
    while time.time() < t_end:
        drain(dl, 0.2)
        dl.write(b"status\n")
        if b"Dialer  MAC" in drain(dl, 0.5): break
    else:
        raise SystemExit(f"Dialer not answering on {a.port}")

    digits = [1, 2, 3, 4, 5, 6, 7, 8, 9, 0]
    rows = []
    for i in range(a.rounds):
        d = digits[i % len(digits)]
        before = dial_stats(a.ip)
        drain(dl, 0.05)
        dl.write(f"{d}\n".encode())
        out = drain(dl, 0.5).decode("utf-8", "replace")
        m = re.search(rf"-> digit {d}, ACKed on ch \d+, try (\d+) \((\d+) ms\)", out)
        after = dial_stats(a.ip)
        if not m or after["presses"] == before["presses"] or after["digit"] != d:
            print(f"digit {d}: MISS  dialer: {out.strip()[:90]}")
            rows.append(None)
            continue
        tries, radio = int(m.group(1)), int(m.group(2))
        apply_ms, led_ms = after["applyUs"] / 1000, after["frameUs"] / 1000
        total = radio + led_ms
        rows.append(total)
        print(f"digit {d}: {total:5.1f} ms   radio {radio:2d} ms (try {tries})   apply {apply_ms:4.1f} ms   "
              f"LEDs {led_ms:5.1f} ms" + ("   from FILE" if after["fromFile"] != before["fromFile"] else ""))
        time.sleep(0.3)

    ok = sorted(r for r in rows if r is not None)
    print(f"\n{len(ok)}/{len(rows)} applied", end="")
    if ok:
        print(f"; median {statistics.median(ok):.1f} ms, best {ok[0]:.1f}, worst {ok[-1]:.1f}, "
              f"under 30 ms: {sum(r < 30 for r in ok)}/{len(ok)}")
    else:
        print()

if __name__ == "__main__":
    main()
