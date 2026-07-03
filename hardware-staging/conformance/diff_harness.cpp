// diff_harness.cpp — differential conformance: RTL vs netpak-core, in lockstep.
//
// Drives identical 16-bit register accesses into (a) the Verilated
// n64_netpak gateware and (b) np64::DeviceCore — the ISC reference
// implementation behind the ares device (116k host-test checks) — and diffs
// every read result. This is the "executable conformance harness" from the
// ROADMAP, extended to a third implementation: spec ↔ C++ ↔ RTL.
//
// Scope: the harness compares only offsets inside skeletonScope() — the
// surface the HW-M1 skeleton claims (MAGIC, VERSION, CMD_DATA, unmapped
// gaps). At HW-M2 the scope widens to the full register map and this same
// binary becomes the gateware's acceptance test.
//
// License note: this file drives a GPL-3.0 RTL model and links ISC
// netpak-core; the combined test binary is GPL-3.0 (fine — it's a dev tool,
// never shipped). SPDX-License-Identifier: GPL-3.0-or-later

#include "Vn64_netpak_wrap.h"
#include "verilated.h"

#include "np64/device.hpp"

#include <cstdio>
#include <cstdint>

static Vn64_netpak_wrap* rtl;

static void tick_clk() {
    rtl->clk = 0; rtl->eval();
    rtl->clk = 1; rtl->eval();
    rtl->clk = 0; rtl->eval();
}

static uint16_t rtl_read(uint32_t byteOff) {
    rtl->address = byteOff;      // ADDR_IS_BYTES=1, confirmed vs n64_dd.sv
    rtl->select  = 1;
    rtl->read    = 1;
    rtl->eval();                 // combinational read
    uint16_t v = rtl->rdata;
    rtl->read   = 0;
    rtl->select = 0;
    rtl->eval();
    return v;
}

static void rtl_write(uint32_t byteOff, uint16_t data) {
    rtl->address = byteOff;
    rtl->wdata   = data;
    rtl->select  = 1;
    rtl->write   = 1;
    tick_clk();                  // registered write captures on posedge
    rtl->write   = 0;
    rtl->select  = 0;
    rtl->eval();
}

// The register surface the HW-M1 skeleton implements. Everything else inside
// the window must read 0xFFFF in BOTH models (netpak-core returns 0xFFFF for
// unmapped too), so unmapped gaps are also comparable — but only gaps that
// netpak-core itself leaves unmapped (it implements STATUS/IRQ/... that the
// skeleton doesn't yet). skeletonScope = compare; else skip.
static bool skeletonScope(uint32_t off) {
    if (off < 0x0008) return true;                       // MAGIC, VERSION
    if (off >= 0x0800 && off < 0x0A00) return true;      // CMD_DATA
    if (off >= 0x0064 && off < 0x0100) return true;      // unmapped gap (both)
    if (off >= 0x0A00 && off < 0x1000) return true;      // unmapped gap (both)
    return false;
}

static bool cmdDataScope(uint32_t off) {                 // writable in skeleton
    return off >= 0x0800 && off < 0x0A00;
}

struct Rng {                                             // deterministic fuzz
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    rtl = new Vn64_netpak_wrap;
    rtl->reset = 1; tick_clk(); rtl->reset = 0; tick_clk();

    np64::DeviceCore core;

    long checks = 0, fails = 0;
    auto compare = [&](uint32_t off, const char* phase) {
        uint16_t r = rtl_read(off);
        uint16_t c = core.readHalf(off);
        ++checks;
        if (r != c) {
            ++fails;
            if (fails <= 20)
                std::printf("MISMATCH [%s] off=%04x rtl=%04x core=%04x\n",
                            phase, off, r, c);
        }
    };

    // --- Phase 1: the probe sequence, diffed --------------------------------
    for (uint32_t off : {0x0000u, 0x0002u, 0x0004u, 0x0006u}) compare(off, "probe");
    rtl_write(0x0800, 0xC0FF);      core.writeHalf(0x0800, 0xC0FF);
    rtl_write(0x0802, 0xEE42);      core.writeHalf(0x0802, 0xEE42);
    compare(0x0800, "probe"); compare(0x0802, "probe");

    // --- Phase 2: full in-scope sweep ----------------------------------------
    for (uint32_t off = 0; off < 0x1000; off += 2)
        if (skeletonScope(off)) compare(off, "sweep");

    // --- Phase 3: 200k random lockstep ops ------------------------------------
    Rng rng(0xC0FFEE);
    for (int i = 0; i < 200000; ++i) {
        uint32_t off = (rng.next() % 0x800) * 2;         // halfword offsets < 0x1000
        if (rng.next() & 1 && cmdDataScope(off)) {
            uint16_t d = uint16_t(rng.next());
            rtl_write(off, d);
            core.writeHalf(off, d);
        } else if (skeletonScope(off)) {
            compare(off, "fuzz");
        }
    }

    std::printf("CONFORMANCE (RTL vs netpak-core, skeleton scope): "
                "%ld checks, %ld mismatches\n", checks, fails);
    std::printf(fails == 0 ? "DIFF: PASS\n" : "DIFF: FAIL\n");
    delete rtl;
    return fails ? 1 : 0;
}
