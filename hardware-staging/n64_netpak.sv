// n64_netpak.sv — HW-M1 minimal skeleton (MAGIC / VERSION / CMD_DATA scratch).
//
// Drop into a SummerCart64 fork at fw/rtl/n64/. Answers the netpak-spec.md
// §2.2 probe handshake in the 0x05F0 window so an UNMODIFIED probe.z64 prints
// NP64_PROBE: DETECTED on a real N64. Register semantics: netpak-spec.md §3.
//
// This block is modeled on SC64's peripheral pattern: it connects to the
// n64_reg_bus interface via a `netpak` modport (added alongside dd/cfg/
// flashram — see n64_reg_bus.additions.md). The reg bus is 16-bit (halfword)
// granular, which matches this device's AD16 register model exactly.
//
// ┌── CONFIRMED against SC64's real n64_dd.sv (2026-07-03) ────────────────┐
// │ 1. Clock/reset names: n64_dd uses `input clk, input reset` — matched.  │
// │ 2. reg_bus.address is BYTE-addressed (dd regs at 11'h500/508/50C...),  │
// │    so ADDR_IS_BYTES = 1 (the default) is correct for SC64.             │
// │ HW-M2 note: dd also exposes `output logic irq` (comb OR of sources) —  │
// │ the pattern for our IP3 line — and connects to n64_scb for the         │
// │ firmware/mailbox side.                                                  │
// └────────────────────────────────────────────────────────────────────────┘
//
// SPDX-License-Identifier: GPL-3.0-or-later  (SC64-derivative; the portable
// logic lives in netpak-core under ISC — this thin bus block is fork-local.)

module n64_netpak #(
    // Set to 0 if the reg bus presents a halfword address (one address per
    // 16-bit transfer) instead of a byte address. See CONFIRM item 2. Exposed
    // as a parameter so the testbench can prove the block in BOTH conventions.
    parameter bit ADDR_IS_BYTES = 1'b1
) (
    input logic clk,
    input logic reset,
    n64_reg_bus.netpak reg_bus
);

    // --- Spec §3 constants ---------------------------------------------------
    localparam logic [31:0] MAGIC_VALUE   = 32'h4E50_3634;  // "NP64"
    localparam logic [31:0] VERSION_VALUE = 32'h0000_0000;  // major 0 / minor 0

    // Register byte offsets within the 64 KiB window (spec §3).
    localparam logic [15:0] OFF_MAGIC    = 16'h0000;
    localparam logic [15:0] OFF_VERSION  = 16'h0004;
    localparam logic [15:0] OFF_CMD_DATA = 16'h0800;
    localparam int          CMD_DATA_WORDS = 256;  // 512 bytes / 2 (spec §5.1)

    // Byte offset within the window, regardless of bus addressing convention.
    logic [15:0] boff;
    assign boff = ADDR_IS_BYTES ? reg_bus.address[15:0]
                                : {reg_bus.address[14:0], 1'b0};

    // CMD_DATA scratch — a read-back RAM. This is what makes probe step 3
    // (write a pattern, read it back) pass: proof of a live register file,
    // which open bus on a stock console cannot fake.
    logic [15:0] cmd_data [0:CMD_DATA_WORDS-1];

    logic cmd_data_hit;
    logic [7:0] cmd_data_idx;
    assign cmd_data_hit = (boff >= OFF_CMD_DATA) &&
                          (boff <  OFF_CMD_DATA + 16'(CMD_DATA_WORDS * 2));
    assign cmd_data_idx = (boff - OFF_CMD_DATA) >> 1;

    // --- Reads (combinational; the interface muxes on netpak_select) --------
    // 32-bit registers are two big-endian halves, high half first: byte
    // offset B -> bits [31:16], B+2 -> bits [15:0]. boff[1] picks the half.
    always_comb begin
        reg_bus.rdata = 16'hFFFF;  // spec §3: unmapped offsets read 0xFFFF
        case (boff & 16'hFFFC)
            OFF_MAGIC:   reg_bus.rdata = boff[1] ? MAGIC_VALUE[15:0]
                                                 : MAGIC_VALUE[31:16];
            OFF_VERSION: reg_bus.rdata = boff[1] ? VERSION_VALUE[15:0]
                                                 : VERSION_VALUE[31:16];
            default:     if (cmd_data_hit) reg_bus.rdata = cmd_data[cmd_data_idx];
        endcase
    end

    // --- Writes (CMD_DATA scratch only in this skeleton) --------------------
    // reg_bus.write is already gated by netpak_select in the modport, so no
    // window compare on select is needed here.
    always_ff @(posedge clk) begin
        if (reg_bus.write && cmd_data_hit) begin
            cmd_data[cmd_data_idx] <= reg_bus.wdata;
        end
        // reset intentionally does not clear scratch RAM (not required for the
        // probe; the full block in HW-M2 adds reset/IRQ/ring behavior).
    end

endmodule
