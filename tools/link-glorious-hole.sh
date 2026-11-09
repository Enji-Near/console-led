#!/usr/bin/env bash
# Set up a stock-WLED Glorious-Hole to take digits from Dialer (PoC, see firmware/dialer/README.md):
# enable WLED's ESP-NOW, link Dialer's MAC as a remote, upload remote.json, reboot.
#   tools/link-glorious-hole.sh <settings-PIN> [wled-ip] [remote-macs]
# remote-macs: comma-separated, default Dialer + Relay. WLED replaces the whole list on each save.
# The PIN is only sent to the board; it is not saved anywhere.
# Rerun after any other config change: WLED 16.0.1 clears linked remotes on every /json/cfg POST.
set -euo pipefail
PIN=${1:?usage: $0 <settings-PIN> [wled-ip] [remote-macs]}
IP=${2:-192.168.0.15}
MACS=${3:-48aff36a0898,48aff371942c}   # Dialer, Relay
MAC_JSON=$(python3 -c "import sys,json;print(json.dumps(sys.argv[1].split(',')))" "$MACS")
HERE=$(cd "$(dirname "$0")/.." && pwd)

echo "1/4 enable ESP-NOW, link remotes $MACS"
curl -sf --max-time 10 -H 'Content-Type: application/json' \
  -d "{\"pin\":\"$PIN\",\"nw\":{\"espnow\":true,\"linked_remote\":$MAC_JSON}}" "http://$IP/json/cfg"; echo
echo "2/4 upload remote.json"
curl -sf --max-time 10 -F "data=@$HERE/firmware/glorious-hole/remote.json;filename=/remote.json" "http://$IP/upload"; echo
echo "3/4 check"
curl -sf --max-time 5 "http://$IP/json/cfg" | python3 -c "import json,sys;n=json.load(sys.stdin)['nw'];print(' espnow',n['espnow'],' linked',n['linked_remote']);assert n['espnow']"
curl -sf --max-time 5 "http://$IP/remote.json" | python3 -c "import json,sys;print(' remote.json buttons',sorted(json.load(sys.stdin)))"
if [ "${REBOOT:-1}" = 1 ]; then
  echo "4/4 reboot (ESP-NOW starts on boot; REBOOT=0 to skip when it is already on)"
  curl -sf --max-time 5 -H 'Content-Type: application/json' -d '{"rb":true}' "http://$IP/json/state"; echo
fi
