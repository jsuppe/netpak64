#!/usr/bin/env bash
# Drive headless ares with a real X display + window manager so the desktop-ui
# frontend actually pumps emulation (it gates on window focus and blocks on
# startup modals). Runs an openbox WM under Xvfb, launches ares, and uses
# xdotool to give the window focus and dismiss any modal (Return).
#
# Usage: run_headless.sh <rom.z64> [extra ares args...]
set -u
ROM="$1"; shift || true

export DISPLAY=:99
Xvfb :99 -screen 0 1280x720x24 >/tmp/xvfb.log 2>&1 &
XVFB=$!
sleep 2
openbox >/tmp/openbox.log 2>&1 &
sleep 1

/src/build/desktop-ui/ares \
  --no-file-prompt \
  --setting Input/Defocus=Allow \
  --setting Audio/Driver=None \
  --setting Input/Driver=None \
  --system "Nintendo 64" "$ROM" "$@" >/tmp/ares.log 2>&1 &
ARES=$!

# Keep focusing the ares window and pressing Return to dismiss any modal
# dialog that blocks the emulation loop; also serves as the run duration.
DWELL="${DWELL:-20}"
for i in $(seq 1 "$DWELL"); do
  sleep 1
  WID=$(xdotool search --name "ares" 2>/dev/null | head -1)
  if [ -n "$WID" ]; then
    xdotool windowactivate "$WID" 2>/dev/null
    xdotool key --window "$WID" Return 2>/dev/null
  fi
  # Also dismiss any modal dialog window by class.
  for D in $(xdotool search --class "ares" 2>/dev/null); do
    xdotool windowactivate "$D" 2>/dev/null; xdotool key --window "$D" Return 2>/dev/null
  done
done

sleep 3
kill $ARES 2>/dev/null
kill $XVFB 2>/dev/null
echo "===== ares.log ====="
cat /tmp/ares.log
