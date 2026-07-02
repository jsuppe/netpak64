#!/usr/bin/env python3
"""Apply NetPak64 glue edits to an ares v148 checkout.

Every edit is an exact-match, must-match-exactly-once replacement; any failure
aborts before anything is written. Idempotent: skips edits already applied.
Run from the ares repo root.
"""
import sys
from pathlib import Path

EDITS = [
    # 1a. n64.hpp: np64 headers OUTSIDE namespace ares::Nintendo64 (opens line 42)
    ("ares/n64/n64.hpp",
     "#include <gb/gb.hpp>\n",
     "#include <gb/gb.hpp>\n"
     "#include <np64/device.hpp>\n"
     "#include <np64/backend_loopback.hpp>\n"
     "#include <np64/netclient.hpp>\n"),
    # 1b. n64.hpp: component include INSIDE namespace, after dd
    ("ares/n64/n64.hpp",
     "  #include <n64/dd/dd.hpp>\n",
     "  #include <n64/dd/dd.hpp>\n"
     "  #include <n64/netpak/netpak.hpp>\n"),
    # 1c. n64.hpp: Queue enum member, appended at the END (savestates serialize
    # the queue with raw enum values; appending avoids renumbering GDB_Poll)
    ("ares/n64/n64.hpp",
     "      GDB_Poll,\n    };",
     "      GDB_Poll,\n      NetPak_Tick,\n    };"),
    # 2a. pi/bus.hpp read decode: guarded window ahead of the Dom2 unmapped fallthrough
    ("ares/n64/pi/bus.hpp",
     """  if(address <= 0x0500'05bf) {
    if(_DD()) return dd.ms.read<Size>(address);
    return unmapped;
  }
  if(address <= 0x05ff'ffff) return unmapped;""",
     """  if(address <= 0x0500'05bf) {
    if(_DD()) return dd.ms.read<Size>(address);
    return unmapped;
  }
  if(address >= 0x05f0'0000 && address <= 0x05f0'ffff && netpak.enabled()) {
    return netpak.read<Size>(address);
  }
  if(address <= 0x05ff'ffff) return unmapped;"""),
    # 2b. pi/bus.hpp write decode: same shape
    ("ares/n64/pi/bus.hpp",
     """  if(address <= 0x0500'05bf) {
    if(_DD()) return dd.ms.write<Size>(address, data);
    return;
  }
  if(address <= 0x05ff'ffff) return;""",
     """  if(address <= 0x0500'05bf) {
    if(_DD()) return dd.ms.write<Size>(address, data);
    return;
  }
  if(address >= 0x05f0'0000 && address <= 0x05f0'ffff && netpak.enabled()) {
    return netpak.write<Size>(address, data);
  }
  if(address <= 0x05ff'ffff) return;"""),
    # 3. cpu/cpu.cpp: tick dispatch case
    ("ares/n64/cpu/cpu.cpp",
     "    case Queue::GDB_Poll:      return cpu.gdbPoll();\n",
     "    case Queue::GDB_Poll:      return cpu.gdbPoll();\n"
     "    case Queue::NetPak_Tick:   return netpak.tick();\n"),
    # 4a. system/system.cpp: load after dd
    ("ares/n64/system/system.cpp",
     "  if(_DD()) dd.load(node);\n",
     "  if(_DD()) dd.load(node);\n"
     "  if(!_DD()) netpak.load(node);  //mutually exclusive with 64DD (same bus range)\n"),
    # 4b. unload
    ("ares/n64/system/system.cpp",
     "  if(_DD()) dd.unload();\n",
     "  if(_DD()) dd.unload();\n"
     "  netpak.unload();\n"),
    # 4c. power
    ("ares/n64/system/system.cpp",
     "  if(_DD()) dd.power(reset);\n",
     "  if(_DD()) dd.power(reset);\n"
     "  netpak.power(reset);\n"),
    # 5. n64/CMakeLists.txt: glue sources + netpak-core (paths relative to ares/n64/)
    ("ares/n64/CMakeLists.txt",
     "    dd/dd.cpp\n",
     "    dd/dd.cpp\n"
     "    netpak/netpak.cpp\n"),
    # 6. system/serialization.cpp: savestate hook after dd. Guarded on enabled()
    # so DISABLED sessions keep upstream-compatible savestate layout; a state
    # saved with NetPak enabled requires it enabled to load (64DD-like stance).
    ("ares/n64/system/serialization.cpp",
     "  s(dd);\n",
     "  s(dd);\n"
     "  if(netpak.enabled()) netpak.serialize(s);\n"),
]

# netpak-core is its own static-library target (not sources of `ares`): the
# ares target's source_group(TREE ares/) rejects sources outside ares/, and a
# separate linked target is the shape the real submodule integration wants.
CMAKE_APPEND = """
# NetPak64: device logic lives out-of-tree in thirdparty/netpak-core (np64::)
add_library(netpak-core STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/netpak-core/src/device.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/netpak-core/src/netclient.cpp)
target_include_directories(netpak-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/netpak-core/include)
target_link_libraries(ares PRIVATE netpak-core)
"""

def main():
    root = Path(".")
    changed = 0
    # Pass 1: validate everything before writing anything.
    contents = {}
    for path, old, new in EDITS:
        p = root / path
        text = contents.get(path) or p.read_text()
        contents[path] = text
        if new in text:
            continue  # already applied
        n = text.count(old)
        if n != 1:
            sys.exit(f"ABORT: pattern for {path} matched {n} times (want 1); no files written")
    # Pass 2: apply.
    for path, old, new in EDITS:
        text = contents[path]
        if new not in text:
            contents[path] = text.replace(old, new, 1)
            changed += 1
    cmake = "ares/n64/CMakeLists.txt"
    if "netpak-core/src/device.cpp" not in contents[cmake]:
        contents[cmake] += CMAKE_APPEND
        changed += 1
    for path, text in contents.items():
        (root / path).write_text(text)
    print(f"OK: {changed} edits applied ({len(EDITS)+1 - changed} already present)")

if __name__ == "__main__":
    main()
