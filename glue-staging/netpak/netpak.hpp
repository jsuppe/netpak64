//NetPak64 — virtual network device for homebrew
//Decodes PI Dom2/Addr1 at 0x05f0'0000-0x05f0'ffff (top 64 KiB; clear of 64DD/SC64
//low-range decode). All device logic lives in thirdparty/netpak-core (np64::);
//this glue is bus decode + tick event + IRQ line only. Spec: netpak-spec.md.

struct NetPak : Memory::PI<NetPak> {
  Node::Object node;

  //netpak.cpp
  auto load(Node::Object) -> void;
  auto unload() -> void;
  auto power(bool reset) -> void;
  auto tick() -> void;
  auto updateIrqLine() -> void;
  auto enabled() const -> bool { return _enabled; }

  //io.cpp
  auto readHalf(u32 address) -> u16;
  auto writeHalf(u32 address, u16 data) -> void;
  auto readWord(u32 address) -> u32;
  auto writeWord(u32 address, u32 data) -> void;

  //serialization.cpp
  auto serialize(serializer& s) -> void;

  //queue ticks run at 187.5 MHz (2x CPU clock): 93'750 = 0.5 ms emulated time
  static constexpr u32 TickInterval = 93'750;

private:
  auto log(const char* fmt, ...) -> void;   //NP64_LOG=1 stderr tracing
  auto testSavestateHook() -> void;         //NP64_TEST_SAVESTATE=1 (see netpak.cpp)

  bool _enabled = false;
  bool _logging = false;
  bool _loggedMagicRead = false;
  bool _testSavestate = false;
  u64  _tickCount = 0;
  std::vector<u8> _testState;
  np64::DeviceCore _core;

  //Backend: NP64_RELAY="loopback" (default) uses the deterministic echo
  //backend + local queues; "host:port" spawns the threaded NetClient. The
  //NetClient survives soft reset (session continuity) and is reconstructed
  //on hard power (fresh handshake, new token) — integration doc §8.
  np64::NetClient::Config _netConfig;
  std::unique_ptr<np64::NetClient> _net;
  np64::NetQueue _toNet;      //loopback-mode queues
  np64::NetQueue _fromNet;
  np64::LoopbackBackend<np64::NET_QUEUE_DEPTH> _loopback;
};

extern NetPak netpak;
