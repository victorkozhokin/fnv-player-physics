# Player Physics — where things stand

Written so the state survives a fresh session. Read this and the README and you
are caught up.

## What this is

A fork of [AltimorTASDK/fnv-player-physics](https://github.com/AltimorTASDK/fnv-player-physics),
reworked. `master` is upstream untouched; all of our work is on **`rework`**,
which is the default branch on our fork. `git fetch upstream` still works.

Latest build on the share as `PlayerPhysics-v38.zip`.

## The one thing to understand first

**When this plugin owns the player it does not call the engine's movement code
at all — it replaces it.** Nearly every mistake in this session came from
forgetting that.

It means "do nothing" and "hand the job back" are different actions. Declining
to write a velocity inside `UpdateVelocity` does not return control to the
engine; it leaves whatever velocity was already there, untouched, forever.
Handing back can only happen at `ShouldUsePhysics`, which is the one place the
original still gets called.

## Structure of the main hook

`hook_MoveCharacter` is deliberately in two halves:

1. **Observe** — the interaction watchdog, the ini reload, and the blocked /
   airborne / speed-ratio figures. Runs for the player whether or not the
   plugin is driving them. Writes nothing to the player.
2. **Act** — everything after `if (!ShouldUsePhysics(charCtrl))`.

It was not always so, and that single misplacement was three bugs at once:

- **The MCM toggle worked once, in one direction.** The ini reload sat below
  the ownership test, and `bEnabled=0` makes `ShouldUsePhysics` false — so
  nothing was left running that could ever read `bEnabled=1` again.
- **The interaction watchdog could never fire.** A raised interaction flag is
  *itself* what makes `ShouldUsePhysics` false, so the timeout meant to release
  a flag nobody lowered was unreachable for exactly as long as it was needed.
- **The figures Mantle reads froze** whenever the physics stood down.

## Script interface

Seven zero-argument commands from opcode base `0x6A00`. Mantle depends on all of
them; **a missing one is a compile error in the consuming script, which kills
that whole script rather than one feature of it.** Adding commands is safe,
renaming or removing them is not.

| command | meaning |
|---|---|
| `PPScriptLayerReady` | the loose script is installed; stop using the special-idle fallback |
| `PPBeginInteraction` / `PPEndInteraction` | a mod is steering the player right now |
| `PPBlockedTime` | seconds walking into something and getting nowhere. Exported, unused |
| `PPSpeedRatio` | distance covered over distance asked for, 0 to 1, answered the same frame |
| `PPInAir` | the character controller has left the ground |
| `PPAirTime` | seconds since it left |

`nvse/Plugins/Scripts/ln_PlayerPhysics.txt` is optional. Delete it and the
plugin falls back to standing down for *any* special idle in the load order,
which is what it did before the script layer existed.

## Water

The plugin stands down entirely while swimming — `IsSwimming`, checked inside
`ShouldUsePhysics`.

Two signals, because they answer at different moments: the controller enters
`kState_Swimming` only once the water is deep enough to swim in, and
`kMoveFlag_Swimming` catches wading out of the shallows a little before that.

Two narrower fixes were tried and are worse:

- **Vanilla gravity, model still running.** Fixed the sinking, and then the
  player could swim sideways but never up or down, because the movement model
  has no vertical axis at all — the direction is assembled from forward and
  right.
- **Skipping only `UpdateVelocity`.** See the section above: the player kept
  the momentum they entered the water with, forever.

Giving the model a vertical axis is possible in principle. The engine's own
wanted vector already points where the player is swimming, but it is not in the
same frame as the input this builds, and this struct has produced three wrong
guesses already (below). Vanilla swimming works; the plugin can stay out of it.

## Input

`GetInputVector` picks its source from `kMoveFlag_IsKeyboard`, not from the
contents of any field.

Keyboard uses the digital flags, which are eight compass points and nothing in
between. A controller uses `PlayerMover::moveVector`, which is the real analog
direction — that is what restores diagonals and partial stick deflection.

Two traps, both paid for:

- `moveVector` is stored **strafe first**, the opposite order to the
  forward-first frame the model works in. Read as written, a stick pushed
  forward walks right.
- It is **not zero on a keyboard**. Gating the analog path on the vector's own
  length looked reasonable and made every key walk the player sideways. The
  device flag is a fact; the vector's contents are an inference.

## This struct has lied three times

`CharacterMoveParams` / `PlayerMover` offsets are reverse engineered and each of
these cost a session:

- `move.maxSpeed` at 0x60 is **not** a speed. Values alternated 2000/500 and
  produced a base speed of 13998, which catapulted the player.
- There is no `moveSpeed` field. Rebuilding a speed from cached walk/run values
  and multipliers broke crouching, repeatedly, until the upstream answer was
  read again: **`move.input`'s own length is the wanted speed**, with stance,
  encumbrance, crippled legs and every speed mod already in it.
- `moveVector`'s axis order, above.

When something here needs a new field, prefer a value the engine already
computed over one reconstructed from parts.

## Global code patches

Three byte patches are applied once at load and affect **every character, in
every state**, including while the plugin is stood down. `ShouldUsePhysics`
does not gate them:

- `clearZVelocityOnFall` at `0xCD47F1`, unconditional, re-implemented per
  character in `hook_OnGroundUpdateVelocity`
- `keepGroundZVelocity` at `0xC7386A`, ini `bKeepGroundZVelocity`
- `groundCollision` at `0xC72025` and `0xC7203A`, ini `bBunnyhopGroundCollision`

They were suspects in the swimming bug and are innocent: v34 fixed swimming with
all three still in place. Worth remembering the next time something misbehaves
in a state the plugin claims not to touch.

## Open

- The landing penalty's `airVelocity` is refreshed by `hook_InAirUpdateVelocity`
  every airborne frame regardless of ownership, so it does **not** go stale
  across a stand-down. This was written up as a bug and is not one.
- Nothing else outstanding. Swimming, the MCM toggle and controller input were
  the last three.
