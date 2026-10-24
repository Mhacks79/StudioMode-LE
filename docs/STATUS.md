# STATUS — Studio Mode (Photo Mode LE port)

**Target.** Skyrim LE 1.9.32.0.8, SKSE 1.7.3, MSVC v143 Win32. This is a port of powerof3/PhotoMode (SE, GPL-3.0).

## Decisions

- **Scope of 0.1:** free camera, roll, FOV, camera speed, grids, freeze time, game speed, time of day, timescale, weather, PNG photos. The character tab, filters, overlays, gallery, load screens, MCM and gamepad UI navigation are left for later versions.
- **ImGui Icons (Nexus SE 114790):** a data-only mod, read from `Data\Interface\ImGuiIcons`, the same as on SE. Fonts: the `sFont` Jost font with fa-solid-900 merged in. The key PNGs are decoded with WIC into `D3DPOOL_MANAGED` textures, loaded lazily on the render thread. Without the mod, Segoe UI and `[Key]` text are used. The mod is not redistributed.
- **Overlay:** ImGui 1.92.9 with the DX9 backend, drawn in an `IDirect3DDevice9::Present` vtable hook on the game's device (`[0x01BB0928]`). There is no Win32 platform backend: the cursor comes from the game's own mouse events.
- **Input:** the plugin replaces the call to `EventDispatcher::SendEvent` at `0x00A68F86`. While Photo Mode is open, the list is relinked so that only these reach the game:
  - movement controls: Forward, Back, Strafe Left/Right, Move, Run, and the five free camera controls;
  - Look, but only while the right mouse button is held or the UI is hidden;
  - thumbsticks.

  The links are restored after the dispatch.
- **Free camera:** the same calls as the `tfc` console command (`0x0083E4C0`, then the input context 0xD push/pop).
- **Roll:** the plugin replaces the `FromEulerAnglesZXY` call at `0x0083A54F` in `FreeCameraState::GetRotation`.
- **Photos:** the back buffer is copied before the overlay is drawn. MSAA is resolved with StretchRect, and WIC writes the PNG on a worker thread.
- **Safety:** byte signatures are checked at DataLoaded and nothing is installed on a mismatch. Each engine callback has a fault guard that disables it after 3 faults. Pointer chains are validated (F2). The camera is only toggled on the main thread. The build uses `/EHa`, so locks are released when a guard unwinds.

Evidence: `docs/RE-NOTES.md`.

## Checks actually run

- Every address was read from TESV.exe 1.9.32.0.8 bytes with Capstone (`tools/re/le.py`). There was **no** Ghidra decompilation.
- A MinGW i686 syntax compile and DLL link of all sources succeeds, and both SKSE exports are present. This is not the shipped toolchain.
- The MSVC build runs in GitHub Actions (`.github/workflows/build.yml`).

## Not verified

- ReShade has not been tested.
- Loading a save while Photo Mode is open has not been tested. The user ruled this test out as not a real use case. Photo Mode does still close itself on `PreLoadGame`.

## In-game test, 2026-10-21 (build from b8cae81, user report and log)

**Reported working:**
- opening and closing with Home and Esc;
- free camera movement and look;
- FOV, roll and speed sliders;
- time, freeze and weather list.

**What the log shows:**
- `verify: all 14 sites match`.
- `109 weathers`. This confirms the DataHandler weather array at `+0x290`.
- Another plugin had already redirected `0x00A68F86` to `71D02B30`. The input hook chained to it without problems.
- **Threads:** data loading 19248, input poll 2992, Present 14468. These are three different threads. Following this, interface requests (game speed, forced weather) are now queued and run on the input thread. The fault path compares against the input thread instead of the data-loaded thread.

## In-game test 2, 2026-10-22 (build from ea3dcde)

- Everything reported working, including game speed and weather now that they go through the input thread.
- **ImGui Icons:** the log shows `found; font Jost-Regular.ttf, 36 px; Font Awesome on`.
- **Photos failed:** `writing ... failed, HRESULT 80004005`. The back-buffer copy worked; the WIC write did not. The cause was in `WritePng`: it asked the PNG encoder for 32bpp BGR, which that encoder does not support (it offers 24bpp BGR), and treated the substitution as `E_FAIL`. Fixed: the plugin now accepts the offered format (24bpp BGR, or 32bpp BGRA), and the log names the step that fails.
- Threads were different again: load 4788, data 2268, input 18712, Present 4788.

## In-game test 3, 2026-10-22 (build from f5a7bd6)

- **Photos work.** The log shows `screenshot: wrote ...PhotoMode_20261022_003355_000.png (2560x1440)`, and a second one. The user checked the images.
- The full 0.1 feature set has now been reported working in game, with ImGui Icons installed.

## 0.2 (2026-10-22): Character, Filters, gamepad

- **Character:** the target is the console selection if it is a loaded character, otherwise the player.
  - It is looked up from its FormID every frame and never kept as a pointer.
  - Visibility: the culled bit of `NiAVObject+0x98` on `Get3D` (vtable `+0x1C0`).
  - Expressions, phonemes and modifiers use Mfg Fix NG LE's in-game-accepted model:
    - write the explicit keyframes [1] [2] [3] and the finals [5] [7] [9] under the face's lock at `+0x1A8`;
    - release the `+0x0C` transition through `0x0059D870`;
    - set the `+0x1A3`/`+0x1A4` force edge;
    - re-apply every frame once edited, because a frozen update consumes the edge.
  - Idles: `AIProcess::PlayIdle` `0x0070C4B0`. A pose is stopped with the `ResetRoot` idle.
- **Forms:** LE does not list idles in DataHandler. `SetFormEditorID` (slot 0x33) of `TESIdleForm` (vtable `0x010A5114`) and `TESImageSpaceModifier` (vtable `0x01087484`) is hooked at `SKSEPlugin_Load`. Each table is checked first by its slot-0x32 getter. Names are read through slot 0x32. Paired idles (`pa_`) are left out.
- **Filters:** the plugin's own ps_2_0 pass on the back buffer at Present, compiled at run time by d3dcompiler_47. It runs before the photo is copied and before ImGui draws. Image space modifiers use the engine's `Trigger` `0x004B5A00` and `Stop` `0x004B5CB0` (cdecl), the same calls as `imod`/`rimod`.
- **Gamepad:**
  - ButtonEvent XInput masks are mapped to SKSE key codes.
  - ImGui gamepad navigation: D-pad, A and B.
  - Photo Mode actions follow the SE mod's default buttons. Open is LB+RB.
  - Sticks still pass to the free camera.
- **Text input:** for the idle and effect filters. Scan codes go through `ToUnicodeEx` with the foreground window's layout.
- Checks run: MinGW syntax compile and link; the MSVC build in CI.

## In-game test 4, 2026-10-22 (build from 3c34544)

- The user reports that everything works.
- **The log shows:**
  - `verify: all 20 sites match`;
  - `forms: editor ID collectors idles on / image space modifiers on`, then `4078 idles, 227 image space modifiers`;
  - `filters: colour shader ready`;
  - two photos written at 2560x1440 while the filters were in use.
- Every `character:` line in this log is `Player [00000014]`, so an NPC target is not shown by the log.

## In-game test 5, 2026-10-22 (build from 3c34544, with ENB)

- The user reports that everything works with **ENB**.
- **NPC target confirmed:** `character: Idolaf Guerrier-N\xC9 [0001A689]`. The engine returns the name in the system code page (Windows-1252). The tab now converts it to UTF-8 for ImGui, so the accent displays correctly.

## 0.3 (2026-10-22): character effects

- **Forms:** effect shaders (vtable `0x01082834`, type 0x55) and visual effects (`BGSReferenceEffect`, vtable `0x01086924`, type 0x39) do not keep their editor ID. Slot 0x33 is the TESForm default `0x00C8CCA0` and slot 0x32 returns "". So the slot-0x33 hook copies the name it is handed while loading.
- **Playing:** the same engine calls as the `EffectShader.Play` and `VisualEffect.Play` natives, with a duration of -1:
  - `ApplyEffectShader` `0x004DDE90` and `ApplyArtObject` `0x00531BF0` (cdecl, 8 arguments);
  - a visual effect plays its art object (`+0x14`, with flag bits 1 and 2) and its shader (`+0x18`).
- **Stopping:** what was played is remembered and stopped on reset and on close. This uses `ProcessLists::StopEffectShader` `0x00754840` and `StopArtObject` `0x00754900` (thiscall on `[0x012E32E8]`, `ret 8`), the same calls as the `Stop` natives.

## In-game test 6, 2026-10-22 (build from b6f5676)

- The user reports that the character effects (effect shaders, visual effects, stopping them) all work. No log was sent for this test.

## Studio Mode V1 (1.0.0), 2026-10-22

- Released under the name **Studio Mode**: `StudioMode.dll`, `StudioMode.ini`, photos in `My Games\Skyrim\StudioMode\StudioMode_*.png`, zip `release/StudioModeV1.zip`.
- **Logging removed** at the user's request. `_MESSAGE`, `_WARNING` and `_ERROR` expand to nothing unless `STUDIOMODE_LOG` is defined, and the log file is never opened. The fault guards still disable a hook after 3 faults, but now do it silently.
- The feature set is unchanged from 0.3, which was tested in game.

## Next action

1. Still open: ReShade.
2. Not ported yet: character transforms, overlays, the gallery, load-screen photos.
