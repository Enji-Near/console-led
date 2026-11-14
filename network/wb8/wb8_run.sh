#!/bin/bash
# Run one shell command on the WB8 over the serial console and print its output.
# Usage: run.sh 'command' [timeout_seconds]
# Output is taken between two markers the shell computes ($((1+1)) -> 2), so the
# echoed command line (which the console wraps at 80 columns) never matches them.
D=${WB8_DIR:?set WB8_DIR to the wb8_console.py log directory}
S=$(wc -c < "$D/console.log")
printf 'echo @@$((1+1))BEGIN; %s; echo @@$((1+1))END\r' "$1" > "$D/tx.fifo"
timeout "${2:-20}" bash -c "until tail -c +$((S+1)) '$D/console.log' | grep -a -q '^@@2END'; do sleep 0.2; done"
rc=$?
tail -c +$((S+1)) "$D/console.log" | tr -d '\r' | sed -n '/^@@2BEGIN$/,/^@@2END$/p' | sed '1d;$d'
[ $rc -ne 0 ] && echo "!! timed out waiting for the command to finish"
exit 0
