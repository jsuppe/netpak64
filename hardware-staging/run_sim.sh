#!/usr/bin/env bash
# Simulate the HW-M1 probe sequence against n64_netpak.sv.
# Verified green on Verilator 5.050 (both addressing conventions).
#
#   ./run_sim.sh          # dockerized verilator (no local install needed)
#   ./run_sim.sh local    # use a locally-installed verilator
#
# Expected tail:  NP64_PROBE: DETECTED  /  TB: PASS
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
VFLAGS="--binary --timing -Wno-fatal -Wno-WIDTH -Wno-MULTIDRIVEN --top-module tb_n64_netpak tb_n64_netpak.sv n64_netpak.sv -o simv"

if [[ "${1:-}" == "local" ]]; then
    ( cd "$DIR" && verilator $VFLAGS && ./obj_dir/simv )
else
    IMG=verilator/verilator:latest
    docker run --rm -v "$DIR":/work -w /work "$IMG" $VFLAGS
    docker run --rm -v "$DIR":/work -w /work --entrypoint ./obj_dir/simv "$IMG"
fi
