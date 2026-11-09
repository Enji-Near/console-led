#!/usr/bin/env bash
# Set up a stock-WLED Glorious-Hole to take digits from Dialer (PoC, see firmware/dialer/README.md):
# enable WLED's ESP-NOW, link Dialer's MAC as a remote, upload remote.json, reboot.
#   tools/link-glorious-hole.sh <settings-PIN> [wled-ip] [dialer-mac]
# The PIN is only sent to the board; it is not saved anywhere.
# Rerun after any other config change: WLED 16.0.1 clears linked remotes on every /json/cfg POST.
set -euo pipefail
PIN=${1:?usage: $0 <settings-PIN> [wled-ip] [dialer-mac]}
IP=${2:-192.168.0.15}
MAC=${3:-48aff36a0898}
HERE=$(cd "$(dirname "$0")/.." && pwd)

echo "1/4 enable ESP-NOW, link remote $MAC"
curl -sf --max-time 10 -H 'Content-Type: application/json' \
  -d "{\"pin\":\"$PIN\",\"nw\":{\"espnow\":true,\"linked_remote\":[\"$MAC\"]}}" "http://$IP/json/cfg"; echo
echo "2/4 upload remote.json"
curl -sf --max-time 10 -F "data=@$HERE/firmware/glorious-hole/remote.json;filename=/remote.json" "http://$IP/upload"; echo
echo "3/4 check"
curl -sf --max-time 5 "http://$IP/json/cfg" | python3 -c "import json,sys;n=json.load(sys.stdin)['nw'];print(' espnow',n['espnow'],' linked',n['linked_remote']);assert n['espnow']"
curl -sf --max-time 5 "http://$IP/remote.json" | python3 -c "import json,sys;print(' remote.json buttons',sorted(json.load(sys.stdin)))"
echo "4/4 reboot (ESP-NOW starts on boot)"
curl -sf --max-time 5 -H 'Content-Type: application/json' -d '{"rb":true}' "http://$IP/json/state"; echo
