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
  return _core.readHalf(offset);
}

auto NetPak::readWord(u32 address) -> u32 {
  return _core.readWord(address - 0x05f0'0000);
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
  _core.writeWord(offset, data);
  updateIrqLine();
}
