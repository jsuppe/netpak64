#!/usr/bin/env bash
# Capture one verified four-instance MK64 battle from the NetPak64 harness.
# Run inside the ares-builder container with the harness mounted at /work and
# this repository mounted at /showcase. The script records screenshots only;
# assemble them on the host with the ffmpeg command printed at the end.
set -euo pipefail

FPS=${FPS:-8}
CAPTURE_LIMIT=${CAPTURE_LIMIT:-210}
FRAME_DIR=/work/np64out/showcase-battle-frames
LOG=/work/np64out/showcase-battle.log

rm -rf "$FRAME_DIR"
mkdir -p "$FRAME_DIR"
rm -f "$LOG"

GAMES=1 RACESECS=65 bash /work/battle4g.sh >"$LOG" 2>&1 &
runner=$!

for _ in $(seq 1 30); do
  [ -S /tmp/.X11-unix/X87 ] && break
  sleep 1
done
[ -S /tmp/.X11-unix/X87 ] || { echo "capture: X display never appeared"; exit 1; }

export DISPLAY=:87
delay=$(awk -v fps="$FPS" 'BEGIN { printf "%.5f", 1 / fps }')
limit=$((CAPTURE_LIMIT * FPS))
frame=0

while [ "$frame" -lt "$limit" ]; do
  scrot -o "$FRAME_DIR/f$(printf '%05d' "$frame").png" 2>/dev/null || true
  frame=$((frame + 1))
  if ! kill -0 "$runner" 2>/dev/null; then
    break
  fi
  sleep "$delay"
done

wait "$runner"
echo "capture: $frame frames at ${FPS} fps"
echo "capture: verdict"
tail -n 8 "$LOG"
echo "assemble on host:"
echo "ffmpeg -framerate $FPS -i $FRAME_DIR/f%05d.png -c:v libx264 -crf 20 -pix_fmt yuv420p netpak64-four-player-battle.mp4"
