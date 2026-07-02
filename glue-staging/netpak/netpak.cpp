#include <n64/n64.hpp>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace ares::Nintendo64 {

NetPak netpak;
#include "io.cpp"
#include "serialization.cpp"

//NP64_LOG=1 enables stderr tracing of the device lifecycle and ROM access —
//the scripted-run observability channel (integration doc §9, §11).
auto NetPak::log(const char* fmt, ...) -> void {
  if(!_logging) return;
  va_list args;
  va_start(args, fmt);
  std::fputs("[NetPak64] ", stderr);
  std::vfprintf(stderr, fmt, args);
  std::fputc('\n', stderr);
  va_end(args);
}

//v1 configuration is environment-only (essential for two instances on one
//machine; a settings pane comes later — integration doc §9):
//  NP64_ENABLE=1        attach the device
//  NP64_RELAY=host:port relay backend ("loopback" or unset = echo backend)
//  NP64_ROOM=CODE       6-char join code ("" = create a new room)
//  NP64_NAME=name       node identity (<=15 chars)
auto NetPak::load(Node::Object parent) -> void {
  const char* e = std::getenv("NP64_ENABLE");
  _enabled = e && *e && *e != '0';
  const char* l = std::getenv("NP64_LOG");
  _logging = l && *l && *l != '0';
  const char* t = std::getenv("NP64_TEST_SAVESTATE");
  _testSavestate = t && *t && *t != '0';
  _loggedMagicRead = false;
  _tickCount = 0;
  _testState.clear();
  if(!_enabled) return;                       //disabled: no node, zero diff

  const char* relay = std::getenv("NP64_RELAY");
  const char* room  = std::getenv("NP64_ROOM");
  const char* name  = std::getenv("NP64_NAME");
  _netConfig.relay = relay ? relay : "loopback";
  _netConfig.room  = room ? room : "";
  _netConfig.name  = name ? name : "player";

  node = parent->append<Node::Object>("NetPak64");
  log("device enabled: PI Dom2/Addr1 window 0x05f00000-0x05f0ffff (backend: %s)",
      _netConfig.relay.c_str());
}

auto NetPak::unload() -> void {
  if(!_enabled) return;
  _net.reset();
  node.reset();
  _enabled = false;
}

auto NetPak::power(bool reset) -> void {
  if(!_enabled) return;
  //ares reset=true is a soft reset (console reset button): device registers
  //and rings clear but identity/stats survive, and the relay session (the
  //NetClient) SURVIVES — a mid-match reset is a resync, not a disconnect.
  //Hard power reconstructs the NetClient: fresh handshake, new token.
  _core.reset(!reset);
  if(_netConfig.relay != "loopback") {
    if(!reset || !_net) {
      _net = std::make_unique<np64::NetClient>(_netConfig);
      log("net client %s: relay=%s room='%s' name='%s'",
          reset ? "started" : "restarted (hard power)",
          _netConfig.relay.c_str(), _netConfig.room.c_str(), _netConfig.name.c_str());
    }
  }
  updateIrqLine();
  cpu.queueInsert(Queue::NetPak_Tick, TickInterval);
  log("power(reset=%d): tick armed", reset ? 1 : 0);
}

auto NetPak::tick() -> void {
  if(!_enabled) return;
  static bool first = true;
  if(first) { first = false; log("first tick fired (emulated time is advancing)"); }
  //Run-ahead does an internal save/load storm each frame; suppress all device
  //mutation so it can't shred state (run-ahead is unsupported while attached).
  if(!ares::runAhead()) {
    if(_net) {
      _core.setLink(_net->status() & np64::link::UP);
      _core.tick(_net->inbound(), _net->outbound());
    } else {
      _core.tick(_fromNet, _toNet);       //TX slot -> outbound; ingest inbound
      _loopback.pump(_toNet, _fromNet);   //deterministic echo backend
    }
  }
  updateIrqLine();
  _tickCount++;
  if(_testSavestate) testSavestateHook();
  cpu.queueInsert(Queue::NetPak_Tick, TickInterval);
}

//NP64_TEST_SAVESTATE=1: exercise the exact loadstate sequence mid-session,
//with live traffic, no GUI required. At tick 1000 (~2 s emulated) the device
//is snapshotted; at tick 2000 (~1 s) the snapshot is restored + the standard
//post-load sequence runs (epoch bump, flush, IRQ recompute). A soaking ROM
//must survive the restore and observe the EPOCH change.
auto NetPak::testSavestateHook() -> void {
  struct VecSaver : np64::Serializer {
    std::vector<u8>& v;
    VecSaver(std::vector<u8>& v) : v(v) {}
    auto io(void* data, std::size_t size) -> void override {
      auto p = static_cast<u8*>(data);
      v.insert(v.end(), p, p + size);
    }
  };
  struct VecLoader : np64::Serializer {
    const std::vector<u8>& v; std::size_t pos = 0;
    VecLoader(const std::vector<u8>& v) : v(v) {}
    auto io(void* data, std::size_t size) -> void override {
      memcpy(data, v.data() + pos, size); pos += size;
    }
  };

  if(_tickCount == 1000) {
    _testState.clear();
    VecSaver saver{_testState};
    _core.serialize(saver);
    log("test-savestate: saved %u bytes at tick 1000", (u32)_testState.size());
  }
  if(_tickCount == 2000 && !_testState.empty()) {
    VecLoader loader{_testState};
    _core.serialize(loader);
    _core.epochBump();
    np64::Frame f;
    while(_toNet.pop(f)) {}
    while(_fromNet.pop(f)) {}
    updateIrqLine();
    log("test-savestate: restored at tick 2000; epoch now %u", _core.epoch());
  }
}

auto NetPak::updateIrqLine() -> void {
  //Level-triggered: line = |(IRQ_STATUS & IRQ_MASK), driven on the same
  //cartridge interrupt (CAUSE IP3) the 64DD uses. Only ever called from the
  //emulation thread, so there is no cross-thread IRQ race by construction.
  cpu.setInterruptPending(CPU::Interrupt::Cartridge, _enabled && _core.irqAsserted());
}

}
