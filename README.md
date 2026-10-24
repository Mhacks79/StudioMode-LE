<div align="center">

# Studio Mode

**A photo mode for Skyrim Legendary Edition.**
Free camera, time and weather, character poses, filters, clean PNG photos.

[![Build](https://github.com/Mhacks79/StudioMode-LE/actions/workflows/build.yml/badge.svg)](https://github.com/Mhacks79/StudioMode-LE/actions/workflows/build.yml)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)
![Skyrim LE 1.9.32.0.8](https://img.shields.io/badge/Skyrim-LE%201.9.32.0.8-5ab4f0)
![SKSE 1.7.3](https://img.shields.io/badge/SKSE-1.7.3-5ab4f0)
![x86](https://img.shields.io/badge/arch-x86-lightgrey)

[**Download**](https://github.com/Mhacks79/StudioMode-LE/releases/latest) ·
[Changelog](CHANGELOG.md) ·
[Reverse engineering notes](docs/RE-NOTES.md) ·
[Support on Ko-fi](https://ko-fi.com/mhacks79)

</div>

---

Studio Mode is a native [SKSE](https://skse.silverlock.org/) plugin that ports
[**Photo Mode**](https://github.com/powerof3/PhotoMode) by **powerof3** (Skyrim Special Edition) to the original
**Skyrim Legendary Edition** (`TESV.exe` 1.9.32.0.8, SKSE 1.7.3, 32-bit, Direct3D 9). No ESP is required.

Press one key, fly a free camera anywhere, set the light and the weather, pose your character, add an aura,
tune the colours and save a clean PNG without the interface. Close it and everything Studio Mode changed is
put back as it was.

> Studio Mode works on **Legendary Edition only**. It does not work on Special Edition or Anniversary Edition:
> use the original Photo Mode there.

## Features

- **Free camera** with an on-screen window, usable with the mouse or a gamepad.
- **Camera tab**: composition grids (rule of thirds, golden ratio, diagonals, center cross), field of view,
  camera roll, free camera speed.
- **Time tab**: freeze time, game speed (slow motion), time of day, timescale and forced weather.
- **Character tab**: show or hide the character, expressions, phonemes and face modifiers, idles (poses),
  effect shaders and visual effects (glows, auras, art objects). Works on the player or on the NPC selected in
  the console before opening Studio Mode.
- **Filters tab**: brightness, saturation, contrast, vignette, colour tint and the game's own image space
  effects (drunk, skooma, etc.). The colour filters are Studio Mode's own shader, so they look the same with or
  without ENB.
- **Clean photos**: PNG files in `Documents\My Games\Skyrim\StudioMode`, with the filters, without the Studio Mode
  window or the HUD.
- **Everything is restored** when Studio Mode closes.
- Fonts and key icons from [ImGui Icons](https://www.nexusmods.com/skyrimspecialedition/mods/114790) when it is
  installed (optional).

## Controls

Defaults, all changeable in `StudioMode.ini`.

| Keyboard / mouse | Gamepad | Action |
|---|---|---|
| `Home` | `LB` + `RB` | Open Studio Mode |
| `Esc` or `Home` | `Start` | Close Studio Mode |
| `Print Screen` | `Back` | Take a photo |
| `T` | `X` | Hide / show the window and the HUD |
| `F` | - | Freeze / unfreeze time |
| `R` | `Y` | Reset the current tab |
| `Q` / `E` (`A` / `E` on AZERTY) | `LT` / `RT` | Previous / next tab |
| Right mouse (hold) | Right stick | Turn the camera |
| `WASD` (`ZQSD` on AZERTY) | Left stick | Move the camera |
| Mouse | D-pad, `A`, `B` | Use the window |

To pose an NPC: open the console, click the NPC, close the console, then open Studio Mode.

## Installation

1. Install [SKSE 1.7.3](https://skse.silverlock.org/) for Skyrim Legendary Edition 1.9.32.0.8.
2. Download `StudioMode-<version>.zip` from the [latest release](https://github.com/Mhacks79/StudioMode-LE/releases/latest).
3. Install it with a mod manager (Mod Organizer 2, Vortex), or extract it into your Skyrim `Data` folder so that
   `StudioMode.dll` and `StudioMode.ini` end up in `Data\SKSE\Plugins\`.
4. Optional: install [ImGui Icons](https://www.nexusmods.com/skyrimspecialedition/mods/114790) (the Special Edition
   download works as it is; it only contains fonts and PNG icons).
5. Start the game through SKSE and press `Home`.

## Compatibility

- Tested in game at 2560x1440, with and without ENB.
- At startup, Studio Mode checks every engine address it uses against `TESV.exe` 1.9.32.0.8. If anything differs,
  it installs nothing.
- It hooks the input dispatch call (`0x00A68F86`), the free camera rotation call (`0x0083A54F`) and the Direct3D 9
  device's `Present` / `Reset`. If another plugin has already hooked one of them, Studio Mode calls it in turn.
- Every engine callback runs under a fault guard and is disabled after repeated faults.
- Studio Mode writes no log file by default (see [Building](#building) for a diagnostic build).

## Not in version 1

- Character transforms (moving or turning the character).
- Overlays (frames), the photo gallery and load screen photos.
- An in-game settings menu: Legendary Edition has no SKSE Menu Framework, so settings are in the INI.
- Weather names: weather forms have no editor IDs in LE, so the weather list shows form IDs.

## Building

Requirements: Visual Studio 2022 (v143 toolset, "Desktop development with C++") or the Build Tools. Everything the
project needs is in `extern/`.

```bat
msbuild StudioMode.vcxproj /m /p:Configuration=Release /p:Platform=Win32
```

The result is `build\StudioMode.dll` (x86). To build a diagnostic DLL that writes
`Documents\My Games\Skyrim\SKSE\StudioMode.log`, add `/p:ExtraDefines=STUDIOMODE_LOG`.

[GitHub Actions](.github/workflows/build.yml) builds every push, checks that the DLL is x86 and exports the SKSE
entry points, and attaches the package to the run. Pushing a tag such as `v1.0.0` publishes a GitHub release.

## Repository layout

| Path | Content |
|---|---|
| `src/` | Plugin sources (`PhotoMode.cpp` is the interface, `Hooks.cpp` the engine hooks, `Offsets.h` every address) |
| `extern/` | Dear ImGui (core and DX9 backend) and the SKSE LE headers and `common` library |
| `dist/` | Files shipped next to the DLL (`StudioMode.ini`) |
| `docs/RE-NOTES.md` | Every engine address with the evidence it was read from |
| `docs/STATUS.md` | Decisions, checks actually run, in-game test results, what is not verified |
| `tools/re/le.py` | Capstone helpers used to read `TESV.exe` (`TESV_EXE=<path>`) |

## How the port was done

All engine addresses were established from the Legendary Edition executable by static disassembly (Capstone and
pefile), with calling conventions proved from `ret imm16` and real call sites. Nothing comes from the Special
Edition Address Library. [`docs/RE-NOTES.md`](docs/RE-NOTES.md) lists each one with its evidence, and
[`docs/STATUS.md`](docs/STATUS.md) separates what was tested in game from what was only read from the binary.

## Contributing

Bug reports and findings are welcome, see [`CONTRIBUTING.md`](CONTRIBUTING.md).

## Credits

- **powerof3**: [Photo Mode](https://github.com/powerof3/PhotoMode), the original mod this is ported from (GPL-3.0).
- **Mhacks79**: the Legendary Edition port.
- **Ian Patterson, Stephen Abel and the SKSE team**: SKSE.
- **Omar Cornut and contributors**: [Dear ImGui](https://github.com/ocornut/imgui) (MIT).

See [`NOTICE.md`](NOTICE.md) for the licences. Studio Mode is an independent community port and is not
affiliated with or endorsed by powerof3.

## Licence

[GPL-3.0](LICENSE), as the original.

<div align="center">

[![Support me on Ko-fi](https://storage.ko-fi.com/cdn/brandasset/v2/support_me_on_kofi_blue.png)](https://ko-fi.com/mhacks79)

</div>
