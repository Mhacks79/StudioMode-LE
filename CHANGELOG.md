# Changelog

All notable changes. Versions follow [Semantic Versioning](https://semver.org/).

## [1.0.0] - 2026-10-21

First release as **Studio Mode** (the development builds were called Photo Mode LE).

### Added
- Renamed to Studio Mode: `StudioMode.dll`, `StudioMode.ini`, photos in `Documents\My Games\Skyrim\StudioMode`.
- Logging is compiled out of release builds; define `STUDIOMODE_LOG` for a diagnostic DLL.
- Credits to Photo Mode by powerof3 kept in the sources and documentation, as the GPL-3.0 requires.

## 0.3 - effects on characters
- Effect shaders and visual effects (glows, auras, art objects) on the player or on the selected NPC.
- Editor IDs of effect shaders and reference effects are collected while the game loads them (the game does not
  keep them).
- Accented character names display correctly.
- Tested in game with ENB and with an NPC selected in the console.

## 0.2 - Character and Filters tabs, gamepad
- **Character** tab: show or hide, expressions, phonemes, face modifiers, idles (poses), console-selected NPC.
- **Filters** tab: brightness, saturation, contrast, vignette, colour tint, image space modifiers; own shader.
- Gamepad support for the whole interface.

## 0.1 - first working build
- Free camera with roll, field of view and speed; composition grids.
- Freeze time, game speed, time of day, timescale and forced weather.
- PNG photos with the filters and without the window or the HUD.
- Fonts and key icons from ImGui Icons when installed.
- Startup check of every engine address against `TESV.exe` 1.9.32.0.8; fault guards on every engine callback.
- Fixes found in the first in-game tests: interface engine calls run on the input thread; photo saving accepts the
  PNG encoder's 24 bpp BGR format.

[1.0.0]: https://github.com/Mhacks79/StudioMode-LE/releases/tag/v1.0.0
