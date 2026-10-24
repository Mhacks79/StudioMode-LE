# Reverse engineering notes — Studio Mode (Photo Mode LE port)

Target: `TESV.exe` 1.9.32.0.8, x86, image base `0x00400000`, SHA-256
`d43f3de477ee751caf36c26ee0a20a06225c29c08dfd13c827b537b9ac56cbf2`.

**Method.** Static disassembly with Capstone through `tools/re/le.py`. There was no Ghidra decompilation in the cloud
session. Calling conventions come from `ret imm16` and from real call sites. Nothing here comes from the SE Address
Library. "Verified" means read from the bytes. Exercised in game: see "Established in game" below.

The executable is never committed. Set `TESV_EXE` to point `le.py` at your own copy.

## Free camera

| What | Address | Evidence |
|---|---|---|
| `tfc` console command | entry `0x0124CB98`, exec `0x0053B300` | console command table `0x0124B748..0x0124E858` (stride 0x28, SKSE `CommandInfo`) |
| `PlayerCamera::ToggleFreeCamera(bool freezeTime)` | `0x0083E4C0` | thiscall, `ret 4`. The tfc handler calls it with `ecx = [0x012E7288]`. On the "entered" path it writes the argument to `[[0x01B2E860]+0x0A]`; on the "left" path it writes 0 (`0x0083E54E`/`0x0083E55F`) |
| Input context push/pop | `0x00A67260` / `0x00A674D0` | the tfc handler, after the toggle: `push 0xD; ecx=[0x012E7458]`, then push if `[cam+0x20]==[cam+0x78]`, else pop. Both `ret 4` (pop reads its argument at `[esp+0x18]` after 5 pushes) |
| Free state test | `[cam+0x20] == [cam+0x78]` | same handler; `0x78 = 0x6C + 3*4` = `cameraStates[3]` (SKSE `PlayerCamera`) |
| FreeCameraState vtable | `0x010E31F4` | RTTI `.?AVFreeCameraState@@` |
| GetRotation (slot 4) | `0x0083A530` | `FromEulerAnglesZXY(m, [this+0x28], [this+0x24], 0.0)` via `call 0x00AABC90` at **`0x0083A54F`**; the third angle (roll) is always 0 |
| `NiMatrix3::FromEulerAnglesZXY` | `0x00AABC90` | thiscall, `ret 0xC` at `0x00AABDA8`, 27 callers |
| GetTranslation (slot 5) | `0x0083A5B0` | copies `[this+0x18..0x20]` |
| Free camera input handler | vtable `0x010E31DC` (FreeCameraState+0x10) | CanProcess `0x0083A9B0` accepts `[holder+0x58]` (Run) and the strings at `0x01B3E2E0..0x01B3E2F0` |
| `fFreeCameraTranslationSpeed:Camera` | Setting `0x012B1EE8`, value `0x012B1EEC` (20.0) | name pointer at `+8` → `0x010E2D7C`; read by FreeCameraState update at `0x0083A697` |
| World FOV | `[cam+0xAC]` | `SetCameraFOV` exec `0x0053C060` writes `+0xAC` (world) and `+0xB0` (first person) |

## Input

| What | Address | Evidence |
|---|---|---|
| `BSInputDeviceManager` (dispatcher) | `[0x01B2E724]` | SKSE `g_inputEventDispatcher` |
| Poll | `0x00A68F20` | called from the main loop at `0x006937DA` with the frame time |
| Event send call | **`0x00A68F86`** → `0x006EBC10` | `lea ecx,[esp+14]; push ecx; mov ecx,ebp; call SendEvent`. The list head is `[[0x01B40DBC]+0x250]`. Afterwards `0x00A698A0` clears the queue by zeroing its counters and head/tail; it does **not** walk `next` |
| ButtonEvent layout | vtable `0x010D44E0` | pool init `0x00A69230`: vtbl `+0`, device `+4`, type `+8`, next `+0xC`, controlID `+0x10`, keyMask `+0x14` (=-1 initially), value `+0x18`, timer `+0x1C` |
| MouseMoveEvent layout | vtable `0x0110E8F4` | device 1, type 1, keyMask 0xA; `LookHandler::ProcessMouseMove` `0x00770FB0` does `fild [e+0x18]`, `fild [e+0x1C]` → **int32** dx/dy |
| InputStringHolder | `[0x01B2E388]` | `LookHandler::CanProcess` `0x00771940` compares controlID with `[holder+0x18]` ("Look") |
| IsInMenuMode | bytes `0x01B2E85E`, `0x01B2E85F` | Papyrus native `0x00918D90` (registered at `0x00919837` for the string "IsInMenuMode") |

## Time

| What | Address | Evidence |
|---|---|---|
| `Main::freezeTime` | `[[0x01B2E860]+0x0A]` | see ToggleFreeCamera above |
| `BSTimer` | object at `0x01B4ADD0` | `sgtm` exec `0x0052E1C0`: `mov ecx,0x01B4ADD0; call 0x00A4B150` |
| `BSTimer::SetGlobalTimeMultiplier(float, bool)` | `0x00A4B150` | thiscall, `ret 8`; stores to `0x012C2414` and (when the bool is set) `0x012C2410` |
| GameHour / TimeScale | `TESGlobal` 0x38 / 0x3A, value at `+0x20` | `TESGlobal::LoadForm` (vtable `0x01091764` slot 6, `0x00513590`) reads FLTV into `[this+0x20]` at `0x00513686` |
| `LookupFormByID` | `0x00451A30` | SKSE `GameForms.cpp` |

## Weather

| What | Address | Evidence |
|---|---|---|
| `Sky::GetSingleton` | `0x0043B920` | cdecl, lazily creates `[0x01B1160C]` |
| Current weather | `[sky+0x2C]` | `GetIsCurrentWeather` eval `0x00507F80`: `call 0x0043B920; mov eax,[eax+0x2C]`; form type check `0x36` |
| Force weather (queued) | `0x006A3FB0` on `[0x01B38308]` | `ForceWeather` exec `0x0051BD00`: `push override; mov ecx,[0x01B38308]; push weather; call`. Queues task 0x3E through `0x006A2B30`, `ret 8` |
| Release override | `0x0058F8D0` | `ReleaseWeatherOverride` exec `0x00526990`: `call 0x0043B920; mov ecx,eax; call 0x0058F8D0`; clears `[sky+0x38]` |
| Weather forms | `DataHandler` `[0x012E2CB8]` + `0x290` | tArrays by form type from `+0x08` (SKSE layout: keywords, type 4, at `+0x38`; weather is type 0x36). **Unverified in code**: each entry is checked for form type 0x36 at run time |

## Interface and renderer

| What | Address | Evidence |
|---|---|---|
| Show menus (HUD) | `[[0x012E3548]+0x118]` | `ToggleMenus` exec `0x0053A390` flips this byte |
| D3D9 device | `[0x01BB0928]` | `0x00CE7D20`: `mov eax,[0x01B913F0]; mov ecx,[eax+0x38]; mov [0x01BB0928],ecx`; 174 code references use it as `IDirect3DDevice9*` |
| `Direct3DCreate9` | imported by name at run time | string `0x01158284`, referenced at `0x00CD1F91`… (d3d9.dll only imports `D3DPERF_SetOptions` statically) |

## 0.2: characters, forms and image space modifiers

| What | Address | Evidence |
|---|---|---|
| Player | `[0x01B2E8E4]` | SKSE `g_thePlayer` |
| Console selection | handle `[0x01B3E6EC]`, `LookupREFRByHandle` `0x004A9180` (cdecl), invalid handle `[0x01310630]` | SKSE `GetCurrentConsoleRef`; the lookup takes a handle reference, released as in `BSHandleRefObject::DecRefHandle` |
| `Get3D` | TESObjectREFR vtable `+0x1C0` | `Is3DLoaded` native `0x00902950`: `mov edx,[eax+0x1C0]; call edx` |
| Culled flag | `NiAVObject+0x98` bit 0 | 117 `or dword [r+0x98],1` / 108 `and dword [r+0x98],0xFFFFFFFE` in `.text` (full skipdata sweep); SKSE `m_flags` at 0x98 |
| Face data | `actor+0x88 → +0x04 → +0xE0 → +0xDC` | Mfg Fix NG LE (accepted in game), with the keyframes, lock, override and force edge documented there |
| `AIProcess::PlayIdle` | `0x0070C4B0`, thiscall `(actor, 0x40, idle, true, false, target)`, `ret 0x18` | `Actor.PlayIdle` native `0x008DB990` → `0x008DB270`: `ecx=[actor+0x88]`; pushes target, 0, 1, idle, 0x40, actor |
| Idle editor IDs | vtable `0x010A5114`, Get `0x0055DAA0` (`[this+0x34]`), Set `0x0055D990` (`ret 4`) | CommonLibLE `vtables.csv`; bytes re-read |
| IMAD editor IDs | vtable `0x01087484`, Get `0x004B82A0` (`[this+0x1E4]`), Set `0x004B82C0` | CommonLibLE `vtables.csv`; bytes re-read |
| IMAD trigger / stop | `0x004B5A00` cdecl `(imod, float, NiAVObject*)` / `0x004B5CB0` cdecl `(imod)` | `imod` exec `0x0051BD70` pushes `0, strength, imod`; `rimod` exec `0x0051BDE0` pushes `imod` |
| `GetReferenceName` | `0x004DE820` thiscall | SKSE `GameReferences.h` |

| EFSH / RFCT editor IDs | vtables `0x01082834` / `0x01086924`, slot 0x33 = TESForm default `0x00C8CCA0`, slot 0x32 = `0x00401AA0` | RTTI; the name is not stored, so the plugin copies it at load |
| Play effect shader | `0x004DDE90` cdecl `(ref, shader, float duration, facing, 0, 0, 0, 0)` | `EffectShader.Play` native `0x008FAC30` (registered at `0x008FAD41`) |
| Play art object | `0x00531BF0` cdecl `(ref, art, float duration, facing, flags>>1 & 1, flags>>2 & 1, 0, 0)` | `VisualEffect.Play` native `0x008ECEF0`; RFCT art `+0x14`, shader `+0x18`, flags `+0x1C` |
| Stop shader / art | `0x00754840` / `0x00754900` thiscall on `[0x012E32E8]` `(ref, form)`, `ret 8` | `EffectShader.Stop` `0x008FA930`, `VisualEffect.Stop` `0x008ECBB0` |
| Form types | EFSH 0x55, RFCT 0x39 | SKSE `GameForms.h` enum index, the same method that gave weather 0x36 (confirmed in game) |

**Tooling note.** A plain linear Capstone sweep stops at the first data byte in `.text`. The 0.1 notes searched with `xref_imm` (raw byte search), which was unaffected. Whole-section instruction searches need `text_insns()` (skipdata) in `tools/re/le.py`.

## Established in game (2026-10-21)

- Every address above held on the tester's TESV.exe (`verify: all 14 sites match`). The free camera, roll, FOV, time and weather changes were all reported working.
- The DataHandler weather array at `+0x290` returned 109 weather forms, each checked for form type 0x36.
- `0x00A68F86` is hooked by another plugin in a real load order (target `0x71D02B30`). Chaining to it worked.
- **Threads differ.** In the tester's log, `kMessage_DataLoaded` ran on 19248, `BSInputDeviceManager::Poll` on 2992, and `Present` on 14468.

- 0.2 (build 3c34544): all 20 signatures matched. The `SetFormEditorID` collectors gathered 4078 idles and 227 image space modifiers. The d3dcompiler shader was created, and photos were taken with filters active. The user reports Character, Filters and gamepad working.

## Not yet established
- Whether writing `worldFOV` alone is enough while the free camera is active. The console command also calls `0x00B17960` and `0x00C71820`.
- How ENB and ReShade's D3D9 proxies affect the Present hook. The plugin hooks whatever device the game holds.
