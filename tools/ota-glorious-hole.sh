#!/usr/bin/env bash
# Flash firmware/wled/build/firmware.bin to Glorious-Hole over Wi-Fi. Keeps settings and presets.
#   tools/ota-glorious-hole.sh <settings-PIN> [wled-ip] [dialer-mac]
# Unlocking the PIN goes through /json/cfg, which (WLED 16.0.1) also clears linked remotes unless
# they are in the same post, so Dialer's MAC is re-sent with it.
set -euo pipefail
PIN=${1:?usage: $0 <settings-PIN> [wled-ip] [dialer-mac]}
IP=${2:-192.168.0.15}
MAC=${3:-48aff36a0898}
BIN="$(cd "$(dirname "$0")/.." && pwd)/firmware/wled/build/firmware.bin"
[ -f "$BIN" ] || { echo "no $BIN; run firmware/wled/build.sh"; exit 1; }

echo "before: $(curl -sf --max-time 5 "http://$IP/json/info" | python3 -c 'import json,sys;d=json.load(sys.stdin);print(d["ver"],"uptime",d["uptime"],"fx",d["fxcount"])')"
curl -sf --max-time 10 -H 'Content-Type: application/json' \
  -d "{\"pin\":\"$PIN\",\"nw\":{\"linked_remote\":[\"$MAC\"]}}" "http://$IP/json/cfg" >/dev/null
echo "uploading $(stat -c %s "$BIN") bytes..."
curl -s --max-time 180 -F "update=@$BIN;type=application/octet-stream" "http://$IP/update" | sed -e 's/<[^>]*>/ /g' | tr -s ' \n' ' ' | cut -c1-200; echo
echo "waiting for reboot..."
for i in $(seq 1 30); do
  sleep 2
  if INFO=$(curl -sf --max-time 3 "http://$IP/json/info"); then
    echo "after: $(echo "$INFO" | python3 -c 'import json,sys;d=json.load(sys.stdin);print(d["ver"],"uptime",d["uptime"],"fx",d["fxcount"],"um",d["um"])')"
    exit 0
  fi
done
echo "Glorious-Hole did not come back within 60 s"; exit 1
