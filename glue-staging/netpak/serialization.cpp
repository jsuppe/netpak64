//Savestate integration. DeviceCore speaks a host-agnostic np64::Serializer;
//this adapts it onto nall::serializer. Serialized ONLY when enabled, so
//disabled sessions keep upstream-compatible savestate layout (constraint 1);
//a state saved with the device enabled requires it enabled to load, same
//stance as savestates taken with a 64DD attached.

auto NetPak::serialize(serializer& s) -> void {
  struct Adapter : np64::Serializer {
    serializer& s;
    Adapter(serializer& s) : s(s) {}
    auto io(void* data, std::size_t size) -> void override {
      s(std::span<u8>{static_cast<u8*>(data), size});
    }
  } adapter{s};

  _core.serialize(adapter);

  if(s.reading()) {
    //Loadstate discontinuity sequence (netpak-spec.md §7, integration doc §8):
    //restore happened above; now bump the epoch so game netcode can detect
    //the jump, drop any in-flight frames from the pre-restore timeline, and
    //recompute the IRQ line from restored IRQ_STATUS/IRQ_MASK. The relay
    //session itself deliberately lives OUTSIDE the savestate.
    _core.epochBump();
    np64::Frame f;
    if(_net) {
      _net->requestFlush();                  //net thread drops its outbound
      while(_net->inbound().pop(f)) {}       //we own the inbound consumer side
    } else {
      while(_toNet.pop(f)) {}
      while(_fromNet.pop(f)) {}
    }
    updateIrqLine();
    log("unserialize: epoch bumped to %u, in-flight frames flushed", _core.epoch());
  }
}
