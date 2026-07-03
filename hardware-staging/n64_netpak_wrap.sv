// n64_netpak_wrap.sv — flat-port wrapper for Verilator --cc (the differential
// conformance harness drives this; interfaces at the top level are awkward
// for --cc). Contains its own copy of the reg-bus stub — do NOT compile
// together with tb_n64_netpak.sv (which carries an identical stub).
//
// SPDX-License-Identifier: GPL-3.0-or-later

// SIM STUB of SC64's n64_reg_bus — Verilator-safe variant.
// The REAL SC64 interface gates read/write and muxes rdata via modport
// EXPRESSIONS (.read(read && netpak_select), .rdata(netpak_rdata)); Verilator
// does not honor those aliases (it drives/reads the raw signals), so this
// stub uses plain modports and the DRIVER applies select-gating before
// assigning rb.read/rb.write. The DUT source is unchanged either way — it
// references reg_bus.read/write/address/wdata/rdata, which resolve correctly
// against both this stub (plain) and SC64's real interface (expressions).
interface n64_reg_bus ();
    logic        read;
    logic        write;
    logic [16:0] address;
    logic [15:0] rdata;
    logic [15:0] wdata;

    modport netpak (
        input  read,
        input  write,
        input  address,
        output rdata,
        input  wdata
    );
endinterface

module n64_netpak_wrap (
    input  logic        clk,
    input  logic        reset,
    input  logic        select,
    input  logic        read,
    input  logic        write,
    input  logic [16:0] address,
    input  logic [15:0] wdata,
    output logic [15:0] rdata
);
    n64_reg_bus rb ();
    // Select-gating lives here (the real SC64 interface does it in modport
    // expressions, which Verilator ignores — see stub note above).
    assign rb.read    = read  && select;
    assign rb.write   = write && select;
    assign rb.address = address;
    assign rb.wdata   = wdata;
    assign rdata      = rb.rdata;   // solely DUT-driven in this stub

    n64_netpak dut (
        .clk    (clk),
        .reset  (reset),
        .reg_bus (rb)
    );
endmodule
