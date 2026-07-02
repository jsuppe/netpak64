#!/bin/zsh
# NetPak64 netpong demo: two ares instances on this Mac, relay on melchior
# (Tailscale). Watch the AIs rally, or grab a paddle: D-pad / analog stick.
#
# NOTE: after stopping a demo, the relay keeps the old sessions alive for up
# to ~30 s (keepalive GC). Re-running inside that window would join the stale
# room as extra clients — so this script waits out the GC if needed.
ARES=~/dev/ares/build_macos/desktop-ui/Release/ares.app/Contents/MacOS/ares
[ -x "$ARES" ] || ARES=~/dev/ares/build_macos/desktop-ui/Debug/ares.app/Contents/MacOS/ares
ROM=~/dev/roms/netpong.z64
RELAY=${NP64_RELAY:-100.96.183.93:6464}
STAMP=/tmp/netpong-demo.last

if pkill -x ares 2>/dev/null; then sleep 1; fi
if [ -f "$STAMP" ]; then
  AGE=$(( $(date +%s) - $(stat -f %m "$STAMP") ))
  if [ "$AGE" -lt 35 ]; then
    WAIT=$((36 - AGE))
    echo "waiting ${WAIT}s for the relay to forget the previous session..."
    sleep "$WAIT"
  fi
fi
touch "$STAMP"
mkdir -p /tmp/pong1 /tmp/pong2

HOME=/tmp/pong1 NP64_ENABLE=1 NP64_LOG=1 NP64_RELAY=$RELAY \
  NP64_ROOM=DEVROM NP64_NAME=player1 \
  "$ARES" --no-file-prompt --setting Input/Defocus=Allow \
  --system "Nintendo 64" "$ROM" > /tmp/pong1.log 2>&1 &
sleep 4
HOME=/tmp/pong2 NP64_ENABLE=1 NP64_LOG=1 NP64_RELAY=$RELAY \
  NP64_ROOM=DEVROM NP64_NAME=player2 \
  "$ARES" --no-file-prompt --setting Input/Defocus=Allow \
  --system "Nintendo 64" "$ROM" > /tmp/pong2.log 2>&1 &

echo "netpong demo running ($(basename $(dirname $ARES)) build)."
echo "logs: /tmp/pong1.log /tmp/pong2.log | stop: pkill -x ares"
