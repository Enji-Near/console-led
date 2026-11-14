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

## Switch + AP setup (2026-11-14)

`ap-setup.sh` (run on the box). Root password and Wi-Fi key are set in LuCI, never in the repo.
Applied over the serial console: OpenWrt's serial shell has no login (`ttylogin=0`).

- `Console-Net`, WPA2/WPA3 (`sae-mixed`), country `CA`, hostname `console-net-ap`.
- All 5 ports bridged; `lan` is a DHCP client of the home router; firewall, dnsmasq, odhcpd disabled.
- U-Boot `bootcount` (wiki note #1): OpenWrt's `S99bootcount` resets it each boot; checked `bootcount=0`.

### On the home network (2026-11-14)
- WB8 got `192.168.0.174` by DHCP from the home router (`console-net-ap.ht.home`); LuCI reachable there.
- A laptop wired through the WB8 got `192.168.0.175` from the home router: bridging works.
- 100 pings laptop Wi-Fi → router → uplink cable → WB8: 0% loss, avg 13.8 ms, max 143 ms
  (to the router alone: avg 7.2 ms). Uplink cable was suspected flaky; link speed/errors not yet checked.
- The console ESP32 firmware (dialer, sniffer) uses ESP-NOW only and never joins Wi-Fi,
  so it won't appear on Console-Net. Testing Console-Net needs an ESP32 that joins Wi-Fi (e.g. WLED).

## Known issues

- **2.4 GHz radio (MT7602, radio1) crash-looped once** on 25.12.5: `MCU message 02 (seq 3) timed out`
  on the first command at radio start, then `Hardware restart was requested` about once a second
  (111 restarts in ~2 min). Fixed channel 6 didn't help. **Cleared by a cold power cycle; working since.**
  Wiki note #2: the MT7602 reports itself as 0x14c3:0x7612 and needed an mt76 patch.
  - Upstream bug: [openwrt/openwrt#19451](https://github.com/openwrt/openwrt/issues/19451) (open).
    Reporter: 2.4 GHz works on 18.06.9 to 21.02.7, broken from 22.03.0 (24.10.x too). No fix posted.
  - Diagnosis (2026-11-14): the MT7602's interrupts never reach the CPU. `/proc/interrupts`:
    IRQ 29 (01:00.0, MT7612, GIC shared 4) counts normally; IRQ 30 (02:00.0, MT7602, GIC shared 24) stays 0.
    Meanwhile the chip *is* asserting INTx: PCI status `0x0018` (interrupt bit set),
    `INT_SOURCE=0x00300000` with `INT_MASK=0x00703ff3` (read via debugfs `regidx`/`regval`).
    Device tree interrupt-map is standard (port 0/1/2 to GIC 4/24/25), PERST on GPIO 19.
  - The MT7602 is not reset on warm reboot (its BAR keeps `0x60200000`; the MT7612's is cleared),
    but that is **not** the cause: 2.4 GHz worked after warm reboots too.
  - A PCI bus reset (`reset_method=bus`) drops the MT7602 off the bus until power cycle. Don't use it.
  - Test results (2026-11-14), radio1 = 2.4 GHz:

    | Boot | radio1 started | IRQ 30 | Result |
    |---|---|---|---|
    | warm | ~14.5 min after boot (enabled late in LuCI) | 0 | **fails**, restart loop |
    | cold | ~1-2 min after boot | 1492 | works |
    | warm (after bus reset) | at boot (enabled in config) | 4683 | works |
    | warm (plain `reboot`) | at boot | 2384 | works |
    | warm | 15 min idle after boot, then enabled | 1239 | works |
    | (running) | `wifi reload` at ~15 min uptime (= LuCI Save & Apply) | 3025 → 4203 | works |

  - Conclusion: the failure is not caused by warm reboots or idle time. Most likely a bad MT7602
    state was left by the install sequence (cold boot → U-Boot TFTP initramfs → sysupgrade →
    two warm reboots, no power cut). Because warm reboots don't reset the MT7602, the state
    survived until the first power cycle. Not reproduced since.
  - **If 2.4 GHz ever stops working:** check `dmesg | grep "timed out"` or IRQ 30 in
    `/proc/interrupts` staying at 0. Fix: unplug power for 10 s. A software reboot won't clear it.
  - Possibly the same thing in #19451 (versions installed by sysupgrade, i.e. warm reboots). Unconfirmed.

## Images (OpenWrt 25.12.5, checksums in `openwrt-25.12.5-sha256sums`)

- `…samknows_whitebox-v8-initramfs-kernel.bin`: RAM-only test boot (U-Boot option 1).
- `…samknows_whitebox-v8-squashfs-sysupgrade.bin`: 7,799,369 B; firmware partition is 7,872 KiB.

Images are not committed (`*.bin` ignored); download from downloads.openwrt.org.
