#!/usr/bin/env python3
"""Measure Dialer -> Glorious-Hole response time from the laptop.

Sends digits to Dialer over USB serial (as if dialed) and times when Glorious-Hole's WLED
WebSocket reports the new preset. That is an upper bound on radio + WLED time: it includes
WLED pushing the state back to the laptop over Wi-Fi. It does not include the dial itself
(digit-end detection) or the LED frame/transition time.

WLED only pushes state once per second (INTERFACE_UPDATE_COOLDOWN), so digits are 2 s apart.

  tools/measure-latency.py [rounds] [--port /dev/ttyUSB5] [--ip 192.168.0.15]
Needs pyserial (the PlatformIO Python has it: ~/.platformio/penv/bin/python).
"""
import argparse, base64, json, os, socket, statistics, struct, time
import serial

def ws_connect(ip):
    s = socket.create_connection((ip, 80), timeout=5)
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall((f"GET /ws HTTP/1.1\r\nHost: {ip}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
    hdr = b""
    while b"\r\n\r\n" not in hdr:
        hdr += s.recv(1)
    if b" 101 " not in hdr.split(b"\r\n")[0]:
        raise RuntimeError("WebSocket upgrade refused: " + hdr.split(b"\r\n")[0].decode())
    return s

def ws_frames(s):
    """Yield (receive_time, text) for each complete text message. Server frames are unmasked."""
    buf, msg = b"", b""
    while True:
        while True:
            if len(buf) >= 2:
                b1, b2 = buf[0], buf[1] & 0x7F
                off = 2
                if b2 == 126: off, n = 4, (struct.unpack(">H", buf[2:4])[0] if len(buf) >= 4 else None)
                elif b2 == 127: off, n = 10, (struct.unpack(">Q", buf[2:10])[0] if len(buf) >= 10 else None)
                else: n = b2
                if n is not None and len(buf) >= off + n:
                    msg += buf[off:off + n]; buf = buf[off + n:]
                    if b1 & 0x80:                       # FIN: message complete
                        if b1 & 0x0F in (0, 1): yield time.perf_counter(), msg.decode("utf-8", "replace")
                        msg = b""
                    continue
            break
        chunk = s.recv(65536)
        if not chunk: return
        buf += chunk

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rounds", nargs="?", type=int, default=20)
    ap.add_argument("--port", default="/dev/ttyUSB5")
    ap.add_argument("--ip", default="192.168.0.15")
    a = ap.parse_args()

    ws = ws_connect(a.ip); ws.settimeout(3)
    frames = ws_frames(ws)
    next(frames)                                         # initial full state on connect
    dl = serial.Serial(a.port, 115200, timeout=0.05)
    time.sleep(2.5); dl.reset_input_buffer()

    digits = [1, 2, 3, 4, 5, 7, 9, 0]                    # 6 and 8 are playlists: skip
    results = []
    last = None
    for i in range(a.rounds):
        d = digits[i % len(digits)]
        if d == last: d = digits[(i + 1) % len(digits)]
        want = 10 if d == 0 else d
        time.sleep(2.0)                                  # let WLED's 1 s push cooldown expire
        dl.reset_input_buffer()
        t0 = time.perf_counter()
        dl.write(f"{d}\n".encode())
        got = None
        try:
            while got is None:
                t, txt = next(frames)
                try: st = json.loads(txt).get("state", json.loads(txt))
                except ValueError: continue
                if st.get("ps") == want: got = (t - t0) * 1000
        except (socket.timeout, StopIteration):
            pass
        time.sleep(0.1)
        dline = next((l for l in dl.read(2000).decode("utf-8", "replace").splitlines() if "-> digit" in l), "?")
        results.append(got)
        print(f"digit {d}: {'%6.1f ms' % got if got is not None else '  MISS   '}   dialer: {dline}")
        last = d

    ok = [r for r in results if r is not None]
    print(f"\n{len(ok)}/{len(results)} applied", end="")
    if ok:
        ok.sort()
        print(f"; median {statistics.median(ok):.1f} ms, best {ok[0]:.1f}, worst {ok[-1]:.1f}, "
              f"under 30 ms: {sum(r < 30 for r in ok)}/{len(ok)}")
    else:
        print()

if __name__ == "__main__":
    main()
