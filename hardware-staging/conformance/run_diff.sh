#!/usr/bin/env bash
# Differential conformance: Verilated n64_netpak RTL vs np64::DeviceCore.
# Expects a netpak-core checkout (include/, src/) at hardware-staging/netpak-core.
# Dockerized Verilator by default; pass "local" to use a local install.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"      # hardware-staging/conformance
STAGE="$(dirname "$HERE")"                  # hardware-staging
[[ -d "$STAGE/netpak-core/src" ]] || { echo "copy netpak-core (include/,src/) into $STAGE/netpak-core first"; exit 1; }

VARGS=(--cc --build -Wno-fatal -Wno-WIDTH -Wno-MULTIDRIVEN
       --top-module n64_netpak_wrap n64_netpak_wrap.sv n64_netpak.sv
       --exe conformance/diff_harness.cpp netpak-core/src/device.cpp
       -o diff)

if [[ "${1:-}" == "local" ]]; then
    cd "$STAGE"
    verilator "${VARGS[@]}" -CFLAGS "-I$STAGE/netpak-core/include -std=c++17"
    ./obj_dir/diff
else
    IMG=verilator/verilator:latest
    docker run --rm -v "$STAGE":/work -w /work "$IMG" \
        "${VARGS[@]}" -CFLAGS "-I/work/netpak-core/include -std=c++17"
    docker run --rm -v "$STAGE":/work -w /work --entrypoint ./obj_dir/diff "$IMG"
fi
