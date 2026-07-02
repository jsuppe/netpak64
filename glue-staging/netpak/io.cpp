//Bus access: offset-translate into DeviceCore, which owns all register/side-
//effect semantics (netpak-spec.md §3-§4). Word accesses decompose into two
//half accesses inside DeviceCore, so the low-half-completes-the-word rule
//holds identically for CPU stores and PI DMA bursts. Writes can change
//IRQ_STATUS/IRQ_MASK (W1C, mask writes), so the line is recomputed after each.

auto NetPak::readHalf(u32 address) -> u16 {
  const u32 offset = address - 0x05f0'0000;
  if(offset <= 2 && !_loggedMagicRead) {
    _loggedMagicRead = true;
    log("ROM read MAGIC (probe step 1)");
  }
  const u16 v = _core.readHalf(offset);
  if(_traceIo && offset >= 0x2000 && _traceBudget) { _traceBudget--;
    log("io rH RX_WIN+%03x -> %04x (dma)", offset - 0x2000, v); }
  return v;
}

auto NetPak::readWord(u32 address) -> u32 {
  const u32 offset = address - 0x05f0'0000;
  const u32 v = _core.readWord(offset);
  //NP64_TRACE_IO=1: register-level access trace (offsets < 0x100 only, so
  //buffer-window DMA doesn't flood), emulator-side = zero ROM timing impact.
  if(_traceIo && offset < 0x100 && _traceBudget) { _traceBudget--;
    log("io rW %04x -> %08x", offset, v); }
  return v;
}

auto NetPak::writeHalf(u32 address, u16 data) -> void {
  const u32 offset = address - 0x05f0'0000;
  if(offset == 0x001A) log("ROM wrote CMD opcode 0x%02x", data & 0xff);  //low half = dispatch
  _core.writeHalf(offset, data);
  updateIrqLine();
}

auto NetPak::writeWord(u32 address, u32 data) -> void {
  const u32 offset = address - 0x05f0'0000;
  if(offset == 0x0018) log("ROM wrote CMD opcode 0x%02x", data & 0xff);
  if(_traceIo && offset < 0x100 && _traceBudget) { _traceBudget--;
    log("io wW %04x <- %08x", offset, data); }
  _core.writeWord(offset, data);
  updateIrqLine();
}
