#!/usr/bin/env python3
"""Log the Whitebox serial console to a file; stop U-Boot at its menu.

- Everything received is appended (raw) to LOG.
- Unless --log-only is given: when U-Boot prints its operation menu, send "4" once (enter U-Boot
  command line -- read-only, writes nothing to flash).
- Anything written to the FIFO is sent to the console (so commands can be
  issued from another shell: printf 'printenv\n' > FIFO).
"""
import os, sys, threading, serial

PORT = sys.argv[1]
DIR = sys.argv[2]
STOP_AT_UBOOT = "--log-only" not in sys.argv[3:]
LOG = os.path.join(DIR, "console.log")
FIFO = os.path.join(DIR, "tx.fifo")
TRIGGER = b"Please choose the operation"

if not os.path.exists(FIFO):
    os.mkfifo(FIFO)

ser = serial.Serial(PORT, 57600, bytesize=8, parity="N", stopbits=1, timeout=0.05)


def writer():
    while True:
        with open(FIFO, "rb") as f:  # blocks until someone opens for write
            for chunk in iter(lambda: f.read(1), b""):
                ser.write(chunk)


threading.Thread(target=writer, daemon=True).start()

tail = b""
with open(LOG, "ab", buffering=0) as log:
    log.write(b"\n=== logger started ===\n")
    while True:
        data = ser.read(256)
        if not data:
            continue
        log.write(data)
        window = tail + data
        tail = window[-(len(TRIGGER) - 1):]  # enough to catch a split match, never a whole one
        if STOP_AT_UBOOT and TRIGGER in window:
            # Press "4" a few times to make sure one lands inside the countdown.
            ser.write(b"4")
            log.write(b"\n=== logger: sent '4' (U-Boot command line) ===\n")
            tail = b""
