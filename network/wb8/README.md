# SamKnows Whitebox 8 → OpenWrt (switch + Wi-Fi AP PoC)

First proof of concept only: a 5-port switch and Wi-Fi access point for the home office.
The full router design gets its own `/spec` later.

Source: `OpenWrt-Wiki-SamKnows-SK-WB8.pdf` (openwrt.org/toh/samknows/sk-wb8, saved 2026-11-13).

## Serial console (Pico as USB-UART)

Pico runs Raspberry Pi **debugprobe v2.3.1** (`debugprobe_on_pico.uf2`).
UART is GP4 (TX) / GP5 (RX), per `include/board_pico_config.h` in raspberrypi/debugprobe.

| Pico | → | WB8 J2 |
|---|---|---|
| pin 3 GND | → | pin 2 GND |
| pin 6 GP4 (TX) | → | pin 4 Rx (LED end) |
| pin 7 GP5 (RX) | → | pin 3 Tx |
| — | ✗ | pin 1 3V3 (Ethernet end): leave unconnected |

57600 8N1. Port: `/dev/serial/by-id/usb-Raspberry_Pi_Debugprobe_on_Pico__CMSIS-DAP__*-if01`.

`wb8_console.py PORT DIR` logs everything to `DIR/console.log`, presses `4` when the U-Boot
menu appears (U-Boot command line, writes nothing), and sends whatever is written to `DIR/tx.fifo`.

## Findings

- Stock firmware: LEDE, Linux 4.4.79 (2017). Booted normally, not soft-bricked.
- U-Boot 1.1.3 menu appears on a normal power-on with a 4 s countdown; no power-cycling trick needed.
- U-Boot env: `bootdelay=5 ipaddr=10.98.11.62 serverip=10.10.10.3 ethaddr=00:1F:A4:93:DF:E5`.

## Images (OpenWrt 25.12.5, checksums in `openwrt-25.12.5-sha256sums`)

- `…samknows_whitebox-v8-initramfs-kernel.bin`: RAM-only test boot (U-Boot option 1).
- `…samknows_whitebox-v8-squashfs-sysupgrade.bin`: 7,799,369 B; firmware partition is 7,872 KiB.

Images are not committed (`*.bin` ignored); download from downloads.openwrt.org.
