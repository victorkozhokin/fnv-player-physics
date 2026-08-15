# fnv-player-physics

NVSE plugin replacing Fallout: New Vegas' player movement with Quake-style
physics: ground friction, air strafing, additive bunny hops and an impact-based
landing penalty.

Requires **FalloutNV.exe 1.4.0.525** (Steam or GOG) and xNVSE. The plugin refuses
to load on any other build, because every hook address is hardcoded.

## Installing

`PlayerPhysics.dll` goes into `Fallout New Vegas/Data/NVSE/Plugins/`.
`PlayerPhysics.ini` goes into `Fallout New Vegas/Data/Config/PlayerMovement/`,
which the plugin creates on first run if it is missing.

`PlayerPhysics.log` is written next to the ini. Under Mod Organizer 2 the
virtual file system redirects that write, so look in MO2's **Overwrite** folder
rather than in the game directory.

## Configuring

`Data/Config/PlayerMovement/PlayerPhysics.ini` is documented inline. `bEnabled=0` makes the plugin a no-op
without uninstalling it. The `b*` patches at the bottom of the file affect the
engine globally rather than just the player, so they can be disabled
individually if another mod conflicts.

## Building

The plugin links without a C runtime and without the Windows SDK. A handful of kernel32
functions are declared by hand in `src/util/win32.h` and the import library is
generated from `scripts/kernel32.def` at build time. That is what lets it be
built anywhere, with nothing but clang and LLVM's linker.

```sh
brew install lld       # brings llvm, for lld-link and llvm-dlltool
./scripts/build.sh     # any clang will do for compiling, Apple's included
```

The result is `build/PlayerPhysics.dll`. On Linux the same script works with
distro clang and lld; on Windows it works under git-bash.

`cl.exe` is **not** supported: the sources combine clang's MS inline asm with
GNU builtins. `CMakeLists.txt` exists for editor integration and requires
clang too.

## Layout

| path | contents |
| --- | --- |
| `src/game/` | game structures and addresses for 1.4.0.525, with `static_assert`ed offsets |
| `src/util/patch.*` | byte patching that verifies the original bytes before writing |
| `src/util/log.*` | freestanding logger |
| `src/config.*` | ini parsing |
| `src/main.cpp` | the physics and every hook |

### Known workarounds

`bSpecialIdleYieldMovement` disables the custom physics for the duration of
*any* special idle, which is heavier than it should be -- plenty of animation
mods use special idles for things that have nothing to do with moving the
player. It is the current price of coexisting with interaction mods that steer
the player themselves; a narrower condition has not been worked out yet.

### Notes on the game version

All offsets were recovered from the retail 1.4.0.525 executable. Two things the
original source referred to could not be reproduced from public headers:

- `PlayerMover::moveSpeed` — no such field exists; offset `0x88` of `PlayerMover`
  holds an analog move *direction*. Base speed now comes from the engine's
  cached walk/run speeds, with `fBaseSpeed` as a fallback.
- `bhkCharacterListener::collisionTolerance` — the field could not be located,
  so the plugin no longer zeroes it. The related ground-collision code patches
  (`bBunnyhopGroundCollision`) are still applied.
