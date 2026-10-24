# Notices and licences

## Studio Mode

Studio Mode is a port to Skyrim **Legendary Edition** of **Photo Mode** by **powerof3**
(<https://github.com/powerof3/PhotoMode>, Skyrim Special Edition), and is distributed
under the **GNU General Public License, version 3** (see [`LICENSE`](LICENSE), the licence
text of the original repository), as the original is.

- The design, the tabs, the controls and the feature set are those of Photo Mode.
- The LE port (engine addresses and structures read from `TESV.exe` 1.9.32.0.8, Direct3D 9
  rendering, input handling, character, effects and filter features, tests) is by **Mhacks79**.
- This is a modified version of the original work. The changes are the whole of this
  repository's history (`git log`) and are described in [`CHANGELOG.md`](CHANGELOG.md).
- The complete corresponding source of every released build is in this repository.

Studio Mode is an independent community port. It is not affiliated with or endorsed by powerof3.

## Third-party code included in `extern/`

| Component | Where | Licence |
|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) 1.92.9 (core and the DirectX 9 backend) | `extern/imgui/` | MIT, `extern/imgui/LICENSE.txt` |
| SKSE (Skyrim Script Extender) headers and `common` library, by Ian Patterson, Stephen Abel and contributors | `extern/skse-le/` | zlib-style, `extern/skse-le/common/common_license.txt` |

## Not included

- `TESV.exe` and every other file of the game. They are never committed (see `.gitignore`).
- [ImGui Icons](https://www.nexusmods.com/skyrimspecialedition/mods/114790) (fonts and key icons),
  a separate mod that Studio Mode reads from `Data\Interface\ImGuiIcons` when installed.
