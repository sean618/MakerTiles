# Field Protocol (C++23)

An idiomatic C++23 port of the Field Protocol firmware/daemon library. The code
implements a small field-discovery and get/set protocol between a daemon, a bus
master, and many nodes, with a wire format that is preserved byte-for-byte from
the original.

## Building

The project builds under **both `g++` and `clang++`** as standards-conformant
C++23 (no compiler extensions).

### With CMake

```bash
./build.sh            # configures + builds into ./build
cd build && ctest     # runs the test executable
```

This produces two artifacts:

- `fieldprotocol_tests` — the test executable (built with `USE_FP_DAEMON` and
  `USE_MOCK_MICROBUS`). Running it prints `Passed` on success.
- `libmasterNodeSystem` — the shared library exposing the `extern "C"` entry
  points the Python daemon loads via `ctypes` (built with `USE_MOCK_MICROBUS`
  only; no C++ daemon).

### Without CMake

`build_and_test.sh` builds both targets directly with a compiler of your choice
and asserts the suite prints `Passed`:

```bash
./build_and_test.sh g++ clang++   # build + test under both
CXX=g++ ./build_and_test.sh       # or pick one via $CXX
```

## Layout

- `src/` — the protocol library, header-only: `fpCommon.hpp`, `fpNode.hpp`,
  `fpMaster.hpp`, `fpDaemon.hpp`. Each class is fully defined (declaration and
  implementation) in its header, so consumers just `#include` it.
- `test/` — test drivers, board fixtures, and the three interchangeable
  transports (direct-call, mock microbus, real microbus) plus the Python API
  shim (`pythonApi.cpp`).

## What changed from the C version

The original was C-style code compiled as C++. The port keeps the on-the-wire
behaviour identical while adopting modern C++ idioms:

- Everything lives in `namespace fp`. C headers (`stdint.h`, …) became their
  `<cstdint>` equivalents; `NULL` became `nullptr`; casts are now
  `static_cast`/`reinterpret_cast`. `fpCommon.hpp` pulls the fixed-width integer
  types into scope with `using` declarations, so the code writes `uint8_t`
  rather than `std::uint8_t`.
- Tag enums became scoped `enum class : uint8_t`. `FieldFlags` gained
  `operator|`/`&` and `isGettable`/`isSettable`/`isJoined` helpers. The X-macro
  command table became a `commandName()` function.
- The transport, previously a struct of four function pointers plus an opaque
  `void* state`, is now the abstract base class `FpInterface`; each transport
  (mock, direct-call, microbus) derives from it and owns its own state. The old
  `getNodeId` callback folded into a virtual method.
- `tFpNode`/`tFpMaster`/`tFpDaemon` became the classes `Node`/`Master`/`Daemon`
  with `init(...)` constructors-in-spirit and member functions in place of the
  free `fpXxx(&obj, …)` functions. The master's two callbacks became
  `std::function`s.
- Board and field strings became `std::string`, which removed the daemon's
  manual `malloc`/`free` bookkeeping. The daemon now owns discovered boards via
  `std::unique_ptr`.
- The packed wire structs keep `#pragma pack` and their exact field order, so
  `RawBusFieldPacket` (6-byte header + 186-byte payload) and
  `RawDaemonFieldPacket` are byte-identical to the original.

Two latent out-of-bounds reads in the original test fixtures (an 8-byte field
backed by a 1-byte variable, and a 2-byte-element stream backed by a 1-byte
array) were fixed by widening the backing storage to match the declared field
sizes; the wire bytes and field tables are otherwise unchanged.
