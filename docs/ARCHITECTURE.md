# Native port architecture

Goal: a PC executable that runs the original logic of Sega Rally 2 translated
to C++, with its own engine for graphics, sound and controls. The game still
needs your `srally2.zip` to load its data; the repository never contains ROM
data (`data/` is in `.gitignore`). The code base started as the Star Wars
Trilogy port (same Model 3 Step 2.1 board), which itself grew out of the Lost
World port (Step 1.5).

## Pipeline

```
srally2.zip ──romtool.py──▶ data/*.bin   (SegaRally2.exe also reads the zip directly: rom_zip.cpp)
data/crom.bin ──analyze.py──▶ out/listing.asm, functions.txt, report.txt
              ──recomp.py──▶ build/gen/*.cpp  (one C++ function per PowerPC function)
gen/*.cpp + src/ (memory, Real3D, SCSP, inputs, menus) ──CMake/MSVC──▶ SegaRally2.exe
```

`build.bat` runs the whole chain; the generated code is never committed.

## Original hardware (Model 3 Step 2.1) and the port

| Part | Sega Rally 2 | In the port |
|---|---|---|
| CPU | PowerPC 603ev, 166 MHz | Static recompiler (`tools/analyze.py`, `tools/recomp.py`), cycle budget per frame for 166 MHz |
| Fixed CROM | 8 MB at `0xFF800000` | `rom_ptr` maps it; the game copies its program to RAM and runs from there |
| Banked CROM | 8 MB window at `0xFF000000` | Bank register in the Step 2 map |
| PCI bridge | MPC106, config at `0xFEC00000` / `0xFEE00000` | Emulated, folded onto the old MPC105 addresses (`io_addr` in `runtime.cpp`) |
| System, inputs, MIDI, RTC | `0xFExxxxxx` | Emulated (`runtime.cpp`, `inputs.cpp`, `eeprom.cpp`, `jtag.cpp`) |
| Security board | 315-5881 | Hands back the text the game expects (`security_read`) |
| GPU | Real3D Pro-1000, transfers by DMA at `0xC2000000` | DMA emulated; the scene is rebuilt and drawn by a Vulkan renderer (`src/video`) |
| 2D layer | Tilegen at `0xF1000000` | `tilegen.cpp`, with HD replacements (`pack2d`, `text_hd`) |
| Sound | 68000 + 2x SCSP, plus a Digital Sound Board (68000 + MPEG) | `src/audio`: SCSP, DSB2 (second Musashi context + MPEG Layer II decoder) |
| Force feedback | Drive board with its own Z80 (`epr-20512`) | `drive_board.cpp` runs the board's program and its force goes to wheels (SDL haptic); pad rumble is worked out from the car in RAM (`pad_rumble.cpp`) |
| Controls | Steering wheel, pedals, gear shift | Keyboard, gamepads and wheels through SDL3 (`gamepad.cpp`, `wheel.cpp`), keys and buttons chosen in OPTIONS > CONTROLS (`bindings.cpp`) |

## Where things are

- `src/runtime`: the emulated board and the program around it. `runtime.cpp`
  is the memory map, the devices and the per-frame events; `app.cpp` is the
  window side (presentation loop, menus, captures); `app_input.cpp`,
  `app_files.cpp` and `settings_bridge.cpp` help it. `options.cpp` holds the
  command line, `stats.cpp` the records and achievements, `telemetry*.cpp`
  the UDP telemetry, `game_hooks.cpp` the port's code in place of single game
  instructions (listed by address in `tools/game_hooks.py`).
- `src/video`: scene builder (`r3d_scene`, `r3d_world`, `r3d_state`), Vulkan
  renderer (`r3d_renderer`, `r3d_raytrace`, `r3d_dust`), presentation
  (`vk_presenter`, `ui_overlay`), texture and 2D packs, DLSS bridge.
- `src/audio`: SCSP, sound board, DSB2, audio output.
- `src/ui`: the PC menus (`frontend`), achievement toasts, menu music, splash.
- `tests`: unit tests run by CTest and the GitHub workflow.

## What is known about the program

- The reset vector (`0xFFF00100`) leads to the PCI set-up, a copy of the start
  of the CROM to RAM 0 and a jump to `0x100`: the whole game runs from RAM.
- The game's race state is read from RAM, not hooked: the addresses and how
  they were found are in `docs/game_ram.md`.
- `docs/real3d_spec.md` and `docs/sound_spec.md` were written for the Lost
  World port (Step 1.5). The Real3D and SCSP parts still apply; the memory map
  differences are in the table above.
