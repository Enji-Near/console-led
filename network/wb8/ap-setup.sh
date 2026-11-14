#!/bin/sh
# Configure a freshly flashed WB8 (OpenWrt 25.12.5) as a dumb switch + Wi-Fi AP
# on the home network. Run on the WB8 itself (serial console or SSH).
#
# Not set here, on purpose: the root password and the Wi-Fi key. Set those in
# LuCI (System > Administration; Network > Wireless > Edit > Wireless Security)
# so they never land in this repo or in logs.
#
# Result: all 5 ports + both radios bridged; the box gets its own address from
# the home router by DHCP (hostname console-net-ap); no DHCP server, no firewall.
set -e

# --- Step 1: Wi-Fi + hostname -------------------------------------------------
for r in 0 1; do
  uci set wireless.radio$r.country=CA          # Canada; sets legal channels/power
  uci set wireless.radio$r.channel=auto
  uci set wireless.default_radio$r.ssid=Console-Net
  uci set wireless.default_radio$r.encryption=sae-mixed   # WPA2/WPA3
  uci set wireless.default_radio$r.disabled=0
done
# 2.4 GHz (radio1, MT7602) is off for now: on 25.12.5 its firmware times out on the
# first command (MCU message 02) and the radio restarts about once a second.
# Channel fixed to 6 did not help. See README "Known issues".
uci set wireless.radio1.channel=6
uci set wireless.radio1.disabled=1
uci set system.@system[0].hostname=console-net-ap
uci commit wireless
uci commit system

# --- Step 2: become a switch / AP (after the Wi-Fi key is set) ---------------
# The 'wan' port joins the bridge, so every port is a plain switch port.
uci add_list network.@device[0].ports=wan    # @device[0] is br-lan
uci -q delete network.wan || true
uci -q delete network.wan6 || true
uci set network.lan.proto=dhcp               # address comes from the home router
uci -q delete network.lan.ipaddr || true
uci -q delete network.lan.netmask || true
uci -q delete network.lan.ip6assign || true
uci set dhcp.lan.ignore=1                    # no DHCP server here
uci set dhcp.lan.dhcpv6=disabled
uci set dhcp.lan.ra=disabled
uci commit network
uci commit dhcp

for s in firewall dnsmasq odhcpd; do
  /etc/init.d/$s disable
  /etc/init.d/$s stop
done

reload_config
wifi reload
