#!/usr/bin/env bash
# Two-instance NetPak64 dev loop: np64-relay + two headless ares instances in
# one room, cross-instance delivery asserted from their logs. This is the P2
# "chat demo on localhost" milestone (proposal §10).
#
# Runs INSIDE the ares-builder container with --network host (relay runs on
# the container's localhost too). Instances get separate HOMEs (ares settings
# collide otherwise — recon R10 caveat) and separate logs.
#
# Usage: run_two.sh <netchat.z64> <relay-binary>
set -u
ROM="$1"
RELAY="$2"
DWELL="${DWELL:-60}"

export DISPLAY=:99
Xvfb :99 -screen 0 1280x720x24 >/tmp/xvfb.log 2>&1 &
XVFB=$!
sleep 2
openbox >/tmp/openbox.log 2>&1 &
sleep 1

"$RELAY" --dev >/tmp/relay.log 2>&1 &
RELAYPID=$!
sleep 1

launch_ares() {  # $1=instance-id $2=name
  mkdir -p "/tmp/home$1"
  HOME="/tmp/home$1" NP64_ENABLE=1 NP64_LOG=1 \
  NP64_RELAY=127.0.0.1:6464 NP64_ROOM=DEVROM NP64_NAME="$2" \
  /src/build/desktop-ui/ares \
    --no-file-prompt \
    --setting Input/Defocus=Allow \
    --setting Audio/Driver=None \
    --setting Input/Driver=None \
    --system "Nintendo 64" "$ROM" >"/tmp/ares$1.log" 2>&1 &
  echo $!
}

A=$(launch_ares 1 alice)
sleep 2
B=$(launch_ares 2 bob)

# Focus + dismiss modals on every ares window, repeatedly.
for i in $(seq 1 "$DWELL"); do
  sleep 1
  for WID in $(xdotool search --class "ares" 2>/dev/null; xdotool search --name "ares" 2>/dev/null); do
    xdotool windowactivate "$WID" 2>/dev/null
    xdotool key --window "$WID" Return 2>/dev/null
  done
done

kill $A $B $RELAYPID $XVFB 2>/dev/null
sleep 1

echo "===== relay.log (tail) ====="
tail -8 /tmp/relay.log
echo "===== instance A (alice) ====="
grep -E "NetPak64|NP64_CHAT" /tmp/ares1.log | head -12
echo "===== instance B (bob) ====="
grep -E "NetPak64|NP64_CHAT" /tmp/ares2.log | head -12

# Assertions: A (node 0) must receive from node 1; B (node 1) from node 0.
A_OK=$(grep -c "NP64_CHAT: rx from=1" /tmp/ares1.log || true)
B_OK=$(grep -c "NP64_CHAT: rx from=0" /tmp/ares2.log || true)
echo "===== verdict ====="
echo "A received from B: $A_OK packets; B received from A: $B_OK packets"
if [ "$A_OK" -gt 0 ] && [ "$B_OK" -gt 0 ]; then
  echo "NP64_TWO_INSTANCE: PASS"
else
  echo "NP64_TWO_INSTANCE: FAIL"
fi
