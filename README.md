# Player Physics

## [Nexus](https://www.nexusmods.com/newvegas/mods/99081)

Quake-style movement for **Fallout: New Vegas**, in place of the engine's own: ground friction, air strafing, additive bunny hops and a landing penalty that scales with how hard you hit.

A fork of **[AltimorTASDK's Player Physics](https://github.com/AltimorTASDK/fnv-player-physics)**, reworked as a freestanding plugin that builds without MSVC.

## Features

- Air control you can steer a jump with, and hops that chain instead of stopping you dead.
- Landing costs you speed in proportion to the impact, not a flat penalty.
- Three presets — **Responsive**, **Grounded**, **Heavy** — or set the nine values yourself.
- Stands down on its own for swimming, VATS, furniture and any mod that is steering you.
- Optional engine patches for jumping while aiming and for bunny-hop ground collision, each switchable.
- Exports what it knows to other mods, which is what **[Mantle](https://www.nexusmods.com/newvegas/mods/99083)** is built on.

## Requirements

- **FalloutNV.exe 1.4.0.525** (Steam or GOG) — every hook address is hardcoded, and the plugin refuses to load on any other build rather than crash later.
- **xNVSE**

Optional: **MCM Extender** for the menu, **JIP LN NVSE** for the script layer that lets interaction mods say when they are steering you.

## Config

The menu, or `Data\Config\PlayerMovement\PlayerPhysics.ini`, which is documented inline and re-read about once a second. `bEnabled=0` makes the plugin a no-op without uninstalling it.

The three code patches at the bottom of the file are applied once at load, so those need a restart.

## Build

```bash
./scripts/build.sh
```

No MSVC and no Windows SDK — only LLVM. `winget install LLVM.LLVM` on Windows, `brew install lld` on macOS, the distro's clang and lld on Linux.

enjoy ^_^
