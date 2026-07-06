# BG3 Cinematic Combat

XCOM-style cinematic combat camera for **Baldur's Gate 3**: kill cams with slow
motion, crit cams, an over-the-shoulder action cam, hit cams and a dash cam —
with automatic HUD hiding during kill/crit cams for a clean frame — all
configurable ingame via [Mod Configuration Menu](https://www.nexusmods.com/baldursgate3/mods/9162)
(English + German UI).

**Download & installation:** see the mod page on Nexus Mods.

## How it works

The mod has two cooperating halves:

| Part | Tech | Job |
|---|---|---|
| `BG3CinematicCombat.dll` | C++ / MinHook, loaded by [Native Mod Loader](https://www.nexusmods.com/baldursgate3/mods/944) | Hooks the game's camera update and pitch calculation (AOB patterns), drives zoom/pitch/FOV/yaw, slow motion via a QueryPerformanceCounter hook |
| `BG3CinematicCombat.pak` | Script Extender Lua | Detects combat events server-side (action start, impacts, kills, crits) via Osiris + SE events and streams them to the DLL through a JSON file in the Script Extender folder; MCM settings travel the same way |

Notable implementation details, learned the hard way:

- Kill detection cannot rely on post-hit state: at impact time the engine
  already reports fresh victims as dead **and** out of combat. The reliable
  "was alive" proof is captured when the *action starts* (target HP > 0 and
  the target is a real Osiris creature).
- The camera root position is re-derived from the followed character every
  frame — position writes do not survive. Zoom (`desiredZoom`), pitch (via a
  `CalculateCameraPitch` return-value hook) and yaw (`angle`, persistent
  state) are the controllable channels.
- When a kill ends the fight, the engine re-targets the camera to the main
  character *while the slow motion is still running*. The pan goal is
  re-derived every frame from the `GameCameraBehavior` component's `Targets`
  array — pinning position fields only fights the integrator one step behind
  (visible jitter). The client-side fix replaces `Targets[1]` with the killer
  for the kill-cam window, so the engine computes the pan goal itself and
  there is nothing left to fight.
- The HUD is hidden during kill/crit cams by writing `Visibility` on named
  Noesis widgets (client-side Lua, no key simulation). Stored Noesis node
  references die within seconds — hide *and* restore must do fresh lookups
  by widget name.
- Camera memory layout and hook points are based on the reverse engineering
  work of [ersh1/BG3_NativeCameraTweaks](https://github.com/ersh1/BG3_NativeCameraTweaks) (GPL-3.0).

## Building

Requirements: Visual Studio 2022, CMake ≥ 3.21, [vcpkg](https://github.com/microsoft/vcpkg)
(`VCPKG_ROOT` set), and [LSLib/Divine](https://github.com/Norbyte/lslib)
for packing the pak.

```powershell
# DLL
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
      -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release --parallel

# Pak (Divine.exe from LSLib)
Divine.exe -g bg3 -a create-package --source pak_source --destination BG3CinematicCombat.pak -c lz4
```

Deploy: DLL + `config/BG3CinematicCombat.toml` into `<BG3>\bin\NativeMods\`,
the pak into `%LocalAppData%\Larian Studios\Baldur's Gate 3\Mods\`.

## Credits

- **Norbyte** — [Baldur's Gate 3 Script Extender](https://github.com/Norbyte/bg3se) and LSLib
- **ersh1** — camera reverse engineering ([BG3_NativeCameraTweaks](https://github.com/ersh1/BG3_NativeCameraTweaks), GPL-3.0)
- **Volitio** — [Mod Configuration Menu](https://www.nexusmods.com/baldursgate3/mods/9162)
- **Larian Studios** — Baldur's Gate 3

## License

[GPL-3.0](LICENSE) — this project derives its camera structure knowledge from
GPL-3.0-licensed work by ersh1.
