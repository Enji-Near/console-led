# Gateway hardware: research and proposal

Written 2026-11-13. Input to decision 5 (gateway design) in `02-handoff-architecture.md`.
**Status: Proposed.** Nothing here is decided. Milestone 1 is unaffected.

## Proposal
- **Mesh:** ESP-NOW on channel 6 for nodes and lamps, unchanged (confirmed by `01-intent.md`).
- **Site router:** SamKnows Whitebox 8 running OpenWrt. Its 2.4 GHz AP is pinned to channel 6. Only the
  gateway node and the Console laptop join it.
- **Uplink:** the phone or LTE device plugs into the Whitebox by USB tethering, so the phone's hotspot
  channel never matters. Pixel 7 now. PinePhone with a data SIM later (see Needs-Spec brief).
- **Later:** D-Link DAP-1520 running OpenWrt as a second AP, with a 5 GHz backhaul for larger sites
  (see Needs-Spec brief).
- **Rejected: an all-Wi-Fi node network.** It breaks lamp-os interop (lamps speak ESP-NOW only) and is
  not faster (see below).

## Why our own Wi-Fi does not speed up node control
- ESP-NOW is "connectionless" (Espressif). It goes radio to radio with no AP hop. Infrastructure Wi-Fi
  crosses the air twice (node to AP to node).
- ESP32 station default is `WIFI_PS_MIN_MODEM`. Espressif says receive delay "may be the same as the
  DTIM cycle", which breaks the 30 ms target unless every station sets `WIFI_PS_NONE`.
- Our own AP on channel 6 adds no spectrum. 2.4 GHz congestion at a venue hits it the same.
- What it does fix: control (no captive portal, client isolation, channel changes, DHCP surprises).
  It also gives a stable IP link for the Console, internet access and HTTP OTA.

## Risk to design around
- Only the gateway node joins the router. The handoff notes WLED restarts its own AP while searching
  for a lost network. If every node were a station, a router outage could pull the mesh off
  channel 6. **Verify in WLED source** during the source-driven-development step.

## Verified hardware facts
### SamKnows Whitebox 8 (SK-WB8, FCC 2AGPP-SK-WB8V2)
- OpenWrt 25.12.6 builds `samknows_whitebox-v8`: `initramfs-kernel.bin` and `squashfs-sysupgrade.bin`,
  LuCI included. MT7621A, 16 MB flash. The DTS defines separate radios: MT7612 for 5 GHz and MT7603 for 2.4 GHz.
  USB 2.0 port, gigabit WAN and 4 LAN ports. Power 12 V 2 A.
- Serial J2, 57600 8N1, 3.3 V: pin 1 VCC (towards Ethernet, leave unconnected), 2 GND, 3 TX, 4 RX
  (towards LEDs). Source: archived OpenWrt wiki.
- Flash: U-Boot option 1 boots the initramfs image from RAM over TFTP (writes nothing), then
  `sysupgrade -n`. Fallback: stock failsafe `f`, `passwd`, image to `/tmp`, then `sysupgrade -n`.
  Forum defaults were device 10.10.10.1 and server 10.10.10.3. Use whatever the menu shows.
- WB8+ boards share the FCC ID. A WB8+ was flashed to 23.05 with the same image. The flash chip
  differs (W25Q128BV), but it uses the same generic SPI-NOR driver.
- Android USB tethering: `kmod-usb-net-rndis` and `kmod-usb-net-cdc-ncm` (both in 25.12.6 kmods).
  Bake them in with the Firmware Selector, because the box has no internet until tethering works.
- USB-TTL: Pi Pico with `debugprobe_on_pico.uf2` (debugprobe v2.3.1). UART TX is GP4, RX is GP5. It follows
  the host baud. 3.3 V I/O.

### Uplink devices
- **CF-33 laptop:** Sierra EM7455 LTE is installed (seen by ModemManager on this machine).
  LTE B1-5, 7, 12, 13, 20, 25, 26, 29, 30, 41. No B66. It can share over Ethernet to the Whitebox WAN.
  It is the Console machine, so it is not a permanent on-site gateway.
- **Pixel 7:** USB tethering into the Whitebox. Hotspot channel control is not needed.
- **PinePhone:** see Needs-Spec `pinephone-lte-gateway.md`.

## Open questions
- Does WLED change channel when its STA loses the router? (Verify in source; drives the risk above.)
- Is the gateway always on site, or only when remote access is needed? This decides whether a
  dedicated LTE device (PinePhone) is required.
- SIM: one SIM today (Pixel). A second data SIM is a future consideration, only if needed.
- Does the gateway node link to the Whitebox by Wi-Fi (STA on channel 6) or by wire (Ethernet or USB serial)?
  A wired link keeps the gateway node off the Wi-Fi stack entirely.
- Should the Console reach nodes through the gateway node over IP, or join the mesh directly
  (ESP32 dongle on the laptop)? Latency tests are needed against the 30 ms target.

## Sources
- ESP-NOW: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html
- ESP32 power save: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/wifi-driver/wifi-performance-and-power-save.html
- OpenWrt images: https://downloads.openwrt.org/releases/25.12.6/targets/ramips/mt7621/
- Whitebox DTS: https://github.com/openwrt/openwrt/blob/openwrt-25.12/target/linux/ramips/dts/mt7621_samknows_whitebox-v8.dts
- Archived OpenWrt wiki (pinout): https://oldwiki.archive.openwrt.org/toh/samknows/sk-wb8
- WB8+ 23.05 flash: https://forum.openwrt.org/t/the-samknows-sk-wb8-has-a-new-successor-the-samknows-sk-wb8/159259?page=2
- TFTP/brick thread: https://forum.openwrt.org/t/samknows-sk-wb8-mt7621-bricked-with-latest-snapshot/88877
- debugprobe pins: https://github.com/raspberrypi/debugprobe/blob/master/include/board_pico_config.h
- EM7455 spec: https://dev-source.sierrawireless.com/resources/airprime/minicard/74xx/airprime_em7455_product_technical_specification
