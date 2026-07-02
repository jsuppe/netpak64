# ares glue staging

The NetPak64 in-tree glue for the ares fork lives here in the hub until the
fork gets its own git remote. These files are *applied to* an ares v148
checkout; they are not built from this directory.

## Contents

- `netpak/` — the glue files that become `ares/n64/netpak/`:
  `netpak.hpp`, `netpak.cpp`, `io.cpp`, `serialization.cpp` (thin adapter over
  `np64::DeviceCore` + `np64::NetClient`).
- `apply_glue_edits.py` — idempotent, fail-loud script that makes the in-tree
  edits to existing ares files (run from the ares repo root). Every edit is an
  exact match that must apply exactly once, or the script aborts before writing.
  The touched files are exactly what recon R1–R10 predicted:
  `ares/n64/{n64.hpp, pi/bus.hpp, cpu/cpu.cpp, system/system.cpp, system/serialization.cpp, CMakeLists.txt}`.
- `run_headless.sh` — drives one headless ares so emulation actually advances
  (Xvfb + openbox + xdotool to focus the window and dismiss the startup modal).
- `run_two.sh` — the two-instance dev loop: np64-relay + two ares instances in
  one room, cross-delivery asserted from logs (`NP64_TWO_INSTANCE: PASS`).

## Assembling the fork (on the build host)

```sh
cd ~/dev/ares                      # v148, branch: netpak
# stage netpak-core as the thirdparty submodule contents:
#   thirdparty/netpak-core/{include,src,LICENSE,README.md}
# stage the glue: cp glue-staging/netpak/* ares/n64/netpak/
python3 apply_glue_edits.py        # applies the in-tree edits (idempotent)
# build in the ares-builder container (see ROADMAP "Build environment")
```

## Verifying (P1 step 3)

```sh
# device enabled -> ROM detects it
NP64_ENABLE=1 NP64_LOG=1 bash run_headless.sh probe.z64   # => NP64_PROBE: DETECTED
# device disabled -> zero diff, open bus
bash run_headless.sh probe.z64                            # => NP64_PROBE: NOT_PRESENT
```

## Note on the CMake integration

netpak-core is linked as its own `add_library(netpak-core STATIC ...)` target
rather than added to `ares`'s own source list — the ares target uses
`source_group(TREE ares/)`, which rejects sources outside `ares/`. A separate
linked library is also the shape the real git-submodule integration wants.
