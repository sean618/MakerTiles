# MakerTiles

MakerTiles is a system of modular, snap-together electronic tiles. Each tile is a
small STM32-based board with a single job — a strip of LEDs, a joystick, a motor,
a distance sensor, a screen, a speaker, and so on. Tiles connect on a shared bus,
a USB tile bridges the bus to a PC, and a Python daemon on the PC discovers every
connected tile and lets you read its inputs and drive its outputs.

This repository holds the whole stack: the **hardware** (KiCad board designs), the
**firmware** (STM32 board projects + shared driver libraries), the **protocols**
that move data over the bus, and the **daemon** that talks to it all from a PC.

```
    [ tile ]──[ tile ]──[ tile ]── … ──[ USB tile ]──USB──▶ [ PC / Python daemon ]
       └───────────────────────────────────┘
          shared SPI bus  (MicroBus + Field Protocol)
```

## How it fits together

Two protocols stacked on top of each other carry everything:

- **MicroBus** — the transport. A master/multi-node protocol over a shared
  SPI-style link between microcontrollers, built for speed and very low RAM:
  fixed-size packets, a time-slotted schedule the master broadcasts in every
  packet, and a small per-node sliding window that gives reliable, in-order
  delivery with no dynamic allocation.
- **Field Protocol** — the application layer. Rides inside a MicroBus packet and
  describes each tile as a set of named, typed **fields** (e.g. a LED strip's
  colour, a dial's position). The daemon enumerates tiles, reads their field
  tables, and gets/sets field values without knowing the tile type in advance.

One tile acts as the **master** (the USB tile, which also bridges the bus to a
host PC). Every other tile is a **node**. On the PC, the daemon opens the USB
serial port, discovers the attached tiles, and exposes each one as an object you
can interact with.

## Repository layout

```
hardware/        KiCad designs for every tile (Boards/) + shared symbol/footprint
                 libraries, 3D models and BOMs (Libraries/). Old/ holds retired
                 prototype boards.

firmware/        STM32 firmware.
  Boards/          One STM32CubeIDE project per tile (Button, LedStrip, Motor,
                   Joystick, Screen, USB, …). Mostly STM32G030 nodes; the USB
                   master is an STM32F103.
  Libraries/       Shared C/C++ drivers used across boards (ledStrip, screen,
                   imu, speaker, servos, …) plus helpers (myAssert, fonts).
  Documents/       NewBoardSetup.txt — the checklist for bringing up a new tile
                   (CubeMX peripheral config + firmware wiring).

protocol/        The shared protocol code, host-buildable and testable.
  microbus/        MicroBus transport (C++). Leaf component.
  fieldprotocol/   Field Protocol (C++), depends on microbus. Also built as the
                   masterNodeSystem shared library the Python daemon loads.
  stm32/           STM32 glue binding the protocols to real SPI hardware
                   (stm32F1SpiMaster, stm32Node). Boards reach this via a
                   Core/stm32 symlink.

daemon/          Python daemon (the PC side). Opens the USB serial port, runs
                   board discovery, and drives tiles over the Field Protocol.
                   src/makertiles/ is the package; tests/ holds its test suites.

tests/system/    Full C stack (node + master) over the real MicroBus, in-process.

tools/           Helper scripts (e.g. logparser.py).
```

## The PC daemon

The daemon is a Python package (`makertiles`, src-layout under `daemon/`). At a
high level you point it at a serial port and it hands you back the discovered
tiles:

```python
import makertiles

tiles, manager = makertiles.start(port_name="/dev/ttyUSB0")

tiles.led_strip.colour = makertiles.red   # set an output field
position = tiles.dial.position             # read an input field
```

It can drive the C protocol two ways: over **serial** to real hardware in
production, or — in tests — by loading the `masterNodeSystem` shared library
directly via `ctypes`, so the real Python daemon exercises the real C protocol
without any hardware attached. That same library is what catches drift between
the C and Python sides of the wire contract.

Optional extras: `Pillow` (`images`) for the screen image-drawing helpers.

## Building and testing

The C components and the Python daemon build in one CMake tree, and every test
suite runs from a single command:

```bash
./build_and_test.sh
```

This configures, builds (MicroBus, the Field Protocol + its shared library, and
the system test), then runs `ctest`. Python test deps (`pytest`, `pyserial`) are
installed automatically if missing.

To re-run only the failures, verbosely:

```bash
ctest --test-dir build --rerun-failed --output-on-failure
```

### Test suites

| CTest name           | What it covers                                              |
|----------------------|-------------------------------------------------------------|
| `microbus_unit`      | MicroBus transport unit tests (host).                       |
| `fieldprotocol_unit` | Field Protocol unit tests against a mock MicroBus.          |
| `system_integration` | Full C stack (node + master) over the real MicroBus.        |
| `python_unit`        | Python Field-Protocol encode/decode round trips.            |
| `python_integration` | The real Python daemon driving the C protocol via the .so.  |

`python_integration` is the suite that exercises both languages together, so it
is the one that catches C↔Python wire-contract drift.

The firmware board projects under `firmware/Boards/` are separate
STM32CubeIDE/CMake projects and are **not** part of the host build above — they
are flashed to real tiles. See `firmware/Documents/NewBoardSetup.txt` for the
per-board hardware and firmware bring-up checklist.

## Building firmware for a tile

Each `firmware/Boards/<Tile>/` directory is its own STM32 project (`.ioc` for
CubeMX, a linker script for the target part, and CubeIDE debug launch configs).
Nodes are generally STM32G030; the USB master is an STM32F103. The protocol
sources are shared into each board via symlinks (`Core/stm32`,
`Core/Libraries`, etc.) rather than copied — see the setup checklist for the
exact links and the required SPI/timer/interrupt peripheral configuration.

## License

Released under the [MIT License](LICENSE).

