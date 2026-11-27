<h1 align="center">Nico SR2</h1>

<p align="center">
  <b>An unofficial, fan-made recompilation of the 1998 arcade game Sega Rally 2.</b><br>
  Not an emulator: the original program, recompiled to C++ on your own PC from your own copy, with a modern Vulkan renderer.
</p>

> [!IMPORTANT]
> **Unofficial fan project.** Not affiliated with, endorsed or sponsored by SEGA. Nothing from the
> original game is in this repository: no ROM, no code, no music or voices. To play you need your
> own copy of the arcade game (`srally2.zip`), and the game is built from it on your PC.

<p align="center">
  <a href="https://www.youtube.com/@snicolast"><img src="https://img.shields.io/badge/YouTube-@snicolast-red?logo=youtube&logoColor=white" alt="YouTube: snicolast"></a>
  <a href="https://buymeacoffee.com/snicolast8u"><img src="https://img.shields.io/badge/Buy_me_a_coffee-snicolast8u-FFDD00?logo=buymeacoffee&logoColor=black" alt="Buy me a coffee"></a>
</p>

<p align="center">
  <img src="docs/img/village.jpg" alt="Through a mountain village in true widescreen, with ray-traced sun shadows" width="100%">
</p>

## What you get

- **True widescreen**, not stretched, with the HUD at the corners.
- **Ray tracing**: shadows, global illumination and reflections on the cars.
- **Real dust**: billowing clouds and flying stones instead of the arcade's flat polygon trails.
- **HD liveries and HUD**, redrawn for this port.
- **Sharp and smooth**: NVIDIA DLAA and DLSS, or MSAA, and 120 / 144 Hz and above.
- **Arcade sound**, with surround and headphone modes.
- **Keyboard, gamepad or wheel** (wheel experimental).
- **Achievements and records**: 14 achievements and your best laps per course, in RECORDS.

<table>
  <tr>
    <td><img src="docs/img/forest.jpg" alt="The forest road of the mountain stage"></td>
    <td><img src="docs/img/snow.jpg" alt="Snow thrown up by the wheels on the snowy stage"></td>
  </tr>
</table>

## ⚠️ ROM not included

- Your own **`srally2.zip`** (Model 3 Step 2.0: `epr-20632.17`... plus `epr-20512.bin`, the force feedback board).
- Other versions will not work. The build says which chips are missing.

## Install

<p align="center">
  <a href="https://github.com/snicolast/NicoLauncher-Hub"><img src="docs/img/nicolauncher.png" alt="NicoLauncher" width="480"></a>
</p>

You need Windows 10/11 (64-bit) and a GPU with a Vulkan 1.3 driver (most from 2016 on).

- Ray tracing: NVIDIA RTX 30 / AMD RX 7000 or newer (no integrated GPUs).
- DLAA / DLSS: NVIDIA RTX.
- On other cards those options switch off; the rest is the same.

Install it with **[NicoLauncher](https://github.com/snicolast/NicoLauncher-Hub)**, a free app that
downloads, builds, updates and starts the game for you. No Visual Studio, nothing to set up by hand.

1. Download NicoLauncher, either one:
   - **[NicoLauncher-setup.exe](https://github.com/snicolast/NicoLauncher-Hub/releases/latest/download/NicoLauncher-setup.exe)**, the installer: run it.
   - **[NicoLauncher-portable.zip](https://github.com/snicolast/NicoLauncher-Hub/releases/latest/download/NicoLauncher-portable.zip)**, everything in one folder: unzip it anywhere and run `NicoLauncher.exe`
     ([portable mode](https://github.com/snicolast/NicoLauncher-Hub/blob/main/docs/portable.md)).
2. In the launcher, open **Sega Rally 2**, press **Install** and choose your `srally2.zip` when it asks.
3. Press **Play**.

### Update

NicoLauncher tells you when there is something new. Press **Update**.

## Controls

| | Keyboard | Gamepad |
|---|---|---|
| Steer | Left / Right | Left stick or D-pad |
| Accelerator / brake | Up / Down | RT / LT (or A / B) |
| Shift up / down | A / Z | RB / LB |
| VIEW CHANGE | V | Y |
| Hand brake | Space | X |
| Coin + start | 5, then 1 | BACK, then START |
| Pause | Esc | START |

- **OPTIONS > CONTROLS**: remap the car's keys / buttons.
- PlayStation pads show ✕ ○ □ △. Through Steam Input or DS4Windows: **CONTROLS > BUTTON PROMPTS: PLAYSTATION**.
- F11 or Alt+Enter: fullscreen. "Confirm with the pedal" = accelerator.
- **OPTIONS > EXPERIENCE**: **PC EDITION** (new menus and graphics) or **ARCADE ORIGINAL** (the cabinet as it was).

### Steering wheels (experimental)

- Read through SDL (DirectInput too), with force feedback.
- Reported working: Logitech, Thrustmaster, Fanatec, Moza, Simucube. I have no wheel, so I can't test them.
- **OPTIONS > WHEEL > WHEEL SETUP**: set each control by moving it, plus force feedback, rotation and dead zone.
- Trouble? `set SR2_WHEEL_LOG=1` and open an issue with the log.

<table>
  <tr>
    <td><img src="docs/img/wheel.jpg" alt="OPTIONS > WHEEL: controls, force feedback and the wheel and pedals drawn live"></td>
    <td><img src="docs/img/wheel_setup.jpg" alt="Setting a control by moving it: press the pedal all the way down"></td>
  </tr>
</table>

## Performance

Running slow? In OPTIONS, try these in order:

- **DISPLAY > ANTI-ALIASING: DLSS QUALITY** (RTX cards). Still slow? BALANCED, then PERFORMANCE.
- **GRAPHICS > RAY TRACING: OFF**, a big one.
- **DISPLAY > MAX FPS** lower, or **RENDER SCALE** under 100%.

## Experimental

Untested on real hardware. Feedback welcome in the issues.

- **Telemetry** (OPTIONS > EXPERIENCE > EXPERIMENTAL): speed, RPM, gear and G-forces for SimHub or a motion rig.
  - DiRT Rally format (default): UDP `127.0.0.1:20777`.
  - [OGS-TS](https://github.com/Open-Game-Standards/OGS-Telemetry-Standard): multicast `239.255.17.17:40123`. Thanks to Brian Gilbert.

## More

- `SegaRally2.exe --help`: command-line options.
- Own textures: `--dump-textures` writes them to `out\textures\`; same name in `textures\` replaces it.
- Achievements for [PlayniteAchievements](https://github.com/justin-delano/PlayniteAchievements): `achievements/achievements.csv` (Editor, Spreadsheet Import).
- [How it works](docs/ARCHITECTURE.md): the PowerPC to C++ recompiler.
- Other recompiled arcade games: [The Lost World](https://github.com/snicolast/LostWorldNative)
  and [Star Wars Trilogy](https://github.com/snicolast/StarWarsTrilogyNative).

## Support

If you enjoy the port and want to help with the time and tools behind it, you can buy me a coffee.
Entirely optional: it supports the work on this project, not the game, which belongs to Sega.

<a href="https://buymeacoffee.com/snicolast8u"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me a Coffee" height="50"></a>

## License

The code in this repository is under the [MIT License, with additional terms](LICENSE). The libraries and fonts in
`third_party/` keep their own licenses. The gamepad button pictures in `res/pad/` are
[Controller Input Icons](https://opengameart.org/content/controller-input-icons) by ElDuderino (CC0); the
PlayStation-style ones in `res/pad/ps/` were drawn for this port from their shapes (`res/pad/ps/src/make.py`).
The game itself (ROM, code, data, music and voices) is not included and stays the property of its
owners: you need your own copy of `srally2.zip`.

DLAA and DLSS use the [NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS), under NVIDIA's own license.
NVIDIA's `nvngx_dlss.dll` is always downloaded from NVIDIA. The `dlss_bridge.dll` that NicoLauncher installs
is built from `tools/dlss_bridge` and kept, with NVIDIA's license and notice, in
[NicoLauncher-Hub](https://github.com/snicolast/NicoLauncher-Hub/tree/main/dlss). NVIDIA, DLSS and RTX are
trademarks of NVIDIA Corporation.

**No prebuilt downloads.** The license does not allow anyone to share a compiled build of this
port, a ROM, or anything made from one. I have never published any of them: everyone builds the
game themselves from their own `srally2.zip`. Any ready-to-play download you find elsewhere is
not from me, breaks the license, and is at your own risk.

<sub>A fan project for personal use, not affiliated with or endorsed by SEGA or by any of the car
makers whose cars appear in the game. Sega Rally is a trademark of SEGA; the car names and
liveries belong to their owners.</sub>
