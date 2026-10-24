# Contributing

Thanks for helping keep Skyrim Legendary Edition alive.

## Reporting a bug

Open an [issue](https://github.com/Mhacks79/StudioMode-LE/issues/new/choose) with:

- your Skyrim LE and SKSE versions, and the Studio Mode version (in the release name);
- the steps to reproduce, and whether it still happens with other UI, input or ENB/ReShade mods disabled;
- a screenshot or short video for visual problems;
- the log, if you can: build with `/p:ExtraDefines=STUDIOMODE_LOG` and attach
  `Documents\My Games\Skyrim\SKSE\StudioMode.log` (release builds write no log).

## Engine addresses and findings

This port rests on addresses read from `TESV.exe` 1.9.32.0.8. A finding is welcome when it says **how it was
established**: the calling convention and how it was proved (`ret imm16`, the caller's stack cleanup), the function
it was read in, and whether it was tested in game or only read from the binary. Never copy an address from a
Special Edition source or the Address Library: they do not apply to LE.
`docs/RE-NOTES.md` shows the expected level of evidence.

## Code

- Win32 (x86), MSVC v143, C++17, `/EHa`. Match the style of the surrounding code (tabs, comment density).
- Every new engine address goes in `src/Offsets.h` with its evidence, and a byte signature in `src/Verify.cpp` so the
  plugin refuses to install on a different executable.
- Wrap engine callbacks in the existing fault guards. Do not log unconditionally (logging is compiled out).
- Never commit `TESV.exe` or any other file of the game.
- By contributing you agree that your work is distributed under the GPL-3.0, like the rest of the project.
