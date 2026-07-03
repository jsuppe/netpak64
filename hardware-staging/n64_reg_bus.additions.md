# `n64_reg_bus.sv` additions — wire the NetPak block into SC64's register bus

These are the exact edits to SC64's `fw/rtl/n64/n64_reg_bus.sv` to route the
`0x05F0` window to `n64_netpak`. Modeled on the existing `dd` peripheral so it
composes with SC64's conventions. Add-only — nothing existing is removed.

### 1. Declare the select + rdata (next to the other peripherals)

```systemverilog
    logic flashram_select;
    logic dd_select;
    logic cfg_select;
    logic netpak_select;          // + NetPak64

    logic read;
    logic write;
    logic [16:0] address;
    logic [15:0] rdata;
    logic [15:0] wdata;

    logic [15:0] flashram_rdata;
    logic [15:0] dd_rdata;
    logic [15:0] cfg_rdata;
    logic [15:0] netpak_rdata;     // + NetPak64
```

### 2. Add the select to the `controller` modport output list

```systemverilog
    modport controller (
        output flashram_select,
        output dd_select,
        output cfg_select,
        output netpak_select,     // + NetPak64
        output read,
        output write,
        output address,
        input rdata,
        output wdata
    );
```

### 3. Add the read-data mux term

```systemverilog
    always_comb begin
        rdata = 16'd0;
        if (flashram_select) rdata = flashram_rdata;
        if (dd_select)       rdata = dd_rdata;
        if (cfg_select)      rdata = cfg_rdata;
        if (netpak_select)   rdata = netpak_rdata;   // + NetPak64
    end
```

### 4. Add the `netpak` modport (mirrors `dd`)

```systemverilog
    modport netpak (
        input .read(read && netpak_select),
        input .write(write && netpak_select),
        input address,
        output .rdata(netpak_rdata),
        input wdata
    );
```

---

## 5. Drive `netpak_select` in the bus controller (the "+1 decode route")

`*_select` signals are produced by the reg-bus **controller** — the block that
sees the PI address and decides which peripheral a transaction targets (in
SC64 this is in the PI front-end / decode, e.g. `n64_pi.sv`, *not* in
`n64_reg_bus.sv`). ⚠ open that file and find where `dd_select` is asserted;
add the mirror for our window:

```systemverilog
    // NetPak64 occupies the top 64 KiB of the Dom2/Addr1 (64DD) region.
    // NetPak64 and 64DD are mutually exclusive (netpak-spec.md §2); gate on a
    // build-time or n64_cfg mode bit so exactly one answers this range.
    netpak_select = netpak_mode && (pi_address[31:16] == 16'h05F0);
```

Then instantiate the block at the top level (`n64_top.sv`, beside `n64_dd`):

```systemverilog
    n64_netpak n64_netpak_inst (
        .clk    (clk),          // match n64_dd's actual clock/reset names
        .reset  (reset),
        .reg_bus (reg_bus)       // .netpak modport resolved by the port decl
    );
```

⚠ Confirm from `n64_dd.sv`: the exact clock/reset signal names, and whether
the controller compares a full `pi_address` (as above) or pre-decoded bits.
`address[16:0]` on the reg bus is the low offset the peripheral sees; the
region decode that generates `*_select` uses the upper PI address bits.
