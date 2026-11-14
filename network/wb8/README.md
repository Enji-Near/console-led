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

## Flashing (done 2026-11-14)

1. Laptop USB Ethernet at `192.168.1.2/24`, cable to WB8 `lan1`; dnsmasq as TFTP-only server.
2. At the U-Boot prompt (RAM-only `setenv`, no `saveenv`):
   `setenv ipaddr 192.168.1.20`, `setenv serverip 192.168.1.2`,
   `tftpboot 0x83000000 wb8-initramfs.bin`, `bootm 0x83000000`.
   Loaded high so the 7.6 MB image can't overlap the kernel unpacking at `0x80001000`.
3. In the RAM-booted OpenWrt: both radios found (MT7612 = 5g, MT7602 = 2g), switch up as `lan1-4` + `wan`.
4. Full flash backup over SSH (`cat /dev/mtd0..3`), checksums verified on both ends.
   Kept locally in `stock-flash-backup/`, **gitignored**: it holds SamKnows firmware and this unit's calibration.
5. `scp -O` the sysupgrade image, `sysupgrade -T` (check), then `sysupgrade -n`.
6. Verified: boots from flash; jffs2 overlay persists across reboot.

### Notes
- OpenWrt's `firmware` partition runs to the end of flash, over the stock `recovery` area. The backup is the only copy of it.
- First boot logs `mount_root: failed to sync jffs2 overlay`; harmless (configs present, marker file survived reboot).
- Flash chip ID `ef 40 18` (Winbond W25Q128), not the Spansion part the wiki lists.
- `Failed to find NVMEM device`: LAN MAC is stable (`90:0a:1a:ff:90:ec`) but `wan` gets a random MAC every boot.
  The U-Boot `ethaddr` is `00:1F:A4:93:DF:E5`.
- To go back to stock: TFTP `stock-flash-backup/mtd3-firmware.bin` via U-Boot, or `mtd write` it from OpenWrt.

## Images (OpenWrt 25.12.5, checksums in `openwrt-25.12.5-sha256sums`)

- `…samknows_whitebox-v8-initramfs-kernel.bin`: RAM-only test boot (U-Boot option 1).
- `…samknows_whitebox-v8-squashfs-sysupgrade.bin`: 7,799,369 B; firmware partition is 7,872 KiB.

Images are not committed (`*.bin` ignored); download from downloads.openwrt.org.
