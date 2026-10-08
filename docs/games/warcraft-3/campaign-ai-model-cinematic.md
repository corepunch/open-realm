# Missing Campaign AI, Alarm and Model-Cinematic Natives

## Scope and source contracts

This change implements the Warcraft III `common.j` natives `UnitIgnoreAlarm`,
`UnitIgnoreAlarmToggled` and `PlayModelCinematic`, plus the `common.ai` entry
points `SetAmphibious`, `DisablePathing`, `TeleportCaptain`, and the adjacent
attack-captain movement/query functions. All are registered in the shared
`api_module.c` table; each computer player still has its own AI VM.

The initial goal is to let campaign scripts, including UndeadX08, progress
past missing natives and exhibit sensible behaviour. **`BZ_COMPAT_GUESS`**
comments identify the places where Blizzard's internal engine behaviour is
not documented or where original-game experiments remain necessary. These are
working compatibility approximations, not proven retail algorithms.

## Unit alarms

- `UnitIgnoreAlarm(unit, true)` suppresses the victim unit's automatic
  under-attack notification path (`G_WC3_AttackAlert`), including owner and
  help-request ally messages/pings. It does not change combat or retaliation.
- `UnitIgnoreAlarmToggled` reports the flag, defaulting to false.
- **BZ_COMPAT_GUESS:** the native returns `true` for a valid unit and `false`
  for an invalid unit; retail return-value and exact per-notification scope
  require direct comparison. Normal C edict values survive game saves; no
  persistent pointer or network entity field was added.

## Campaign AI navigation and captains

- `SetAmphibious()` marks the calling AI player's policy (never a global
  switch). Existing movement/pathing in `naval-movement.md` continues to
  route each unit using its authored `movetp`: amphibious Naga can cross water
  and ground-only units cannot. On explicit captain point orders, enabling
  this AI policy performs a **per-member closest-reachable waypoint preflight**
  using that unit's actual movement mask, rather than forcing all members to
  share one land-oriented route. Each order stores the actual waypoint sent
  to each living member; `CaptainAtGoal` checks these reachable waypoints
  instead of waiting for a land unit to cross inaccessible water. The captain's
  authored common goal is preserved. **BZ_COMPAT_GUESS:** partial arrival means
  a unit reached its reachable bank, NOT that it crossed water; if no route can
  be found the member's current coordinate is used. This prevents hung scripts
  but is not Blizzard's undocumented group-level routing algorithm.
- `DisablePathing()` switches off the optional AI preflight when it is active;
  it never disables collision or static pathing enforced by the movement
  system. This separation is intentional; exact retail policy is unknown.
- `TeleportCaptain(x,y)` updates the calling player's attack captain's
  logical position; no soldier is teleported, and home and goal are preserved.
- `CaptainAttack`, `CaptainGoHome`, `CaptainVsUnits`, and `CaptainVsPlayer`
  use normal attack-move/move/target orders for existing assault members.
  Candidates require current fog visibility (`G_FowPlayerCanHoverEntity`),
  including for buildings which otherwise persist in explored fog. The `VsUnits`
  variant excludes structures; `VsPlayer` includes units and buildings. A
  single shared visible target is selected per captain order; member selection
  order cannot silently overwrite the group objective. If nothing eligible is visible, neither invents a target by
  falling back to an unrelated stage waypoint. **BZ_COMPAT_GUESS:** the
  retail targeting preference between these two natives is not fully known.
- `CaptainAtGoal` and `CaptainIsHome` use the captain's explicitly teleported
  logical coordinate when set, even if living members still occupy their old
  locations. A later attack/go-home order clears the override and queries
  compare living members with the requested goal/home. **BZ_COMPAT_GUESS:** their tolerance uses the existing group-flee
  home radius; retail captain arrival and in-flight teleport effects are
  unverified. `ResetCaptainLocs` and `ClearCaptainTargets` are separate
  operations and retain the group roster. `ClearCaptainTargets` clears logical
  targets/routes but preserves soldiers' already-issued orders and does not
  falsely mark an actively fighting captain idle (BZ_COMPAT_GUESS).

## Full-screen model cinematics

- `PlayModelCinematic(modelName)` passes the **authored asset name** from JASS
  to the universal client. The normal game module does not decode or render
  the model; the renderer already resolves `.mdl` to `.mdx`.
- The client queues the model cinematic even if there is no following session
  action, pauses the outgoing simulation, and runs a full-screen isolated MDX
  view. The optional FFmpeg AVI/movie decoder is not required.
- A model sequence timer advances through the MDX sequences and selects the
  matching embedded camera. Playback ends after the last sequence or Escape.
  No sequence loops forever. MDX event-sound/particle handling uses the
  existing renderer; model instance presentation state is released on stop.
  A model scene suspends gameplay music only if it was not already suspended,
  and restores that suspension on natural completion, Escape and disconnect.
  Existing effect sounds stop when the scene starts; the model's own events
  remain active. **BZ_COMPAT_GUESS:** audio isolation mirrors movie playback;
  no special `FinalCinematic.mp3` soundtrack is assumed.
- If JASS requests a map/menu transition after starting playback, that
  transition is performed when playback ends or is skipped, mirroring the
  existing `PlayCinematic` movie interposer. A bounded FIFO queue preserves
  up to eight authored movie/model requests in order, without silently
  replacing one type with the other. Overflow reports a diagnostic and
  rejects the new request while preserving prior items.
- Disconnect and shutdown cancel active playback, clear deferred transitions
  and restore the session's prior pause state. A missing/unloadable asset is
  skipped so it cannot permanently suppress a pending victory or map change.
- **BZ_COMPAT_GUESS:** generic playback starts at sequence 0; Warsmash's
  Arthas/Illidan demo starts at 1. Sequence-to-camera index mapping, order,
  total playback duration and JASS's blocking semantics require retail
  validation. This implementation defers session transitions rather than
  suspending the triggering JASS coroutine.
- **Known visual gap:** Warcraft's MRF/MRD animated cape mesh events are not
  implemented by the current MDX renderer. The cinematic model still renders
  and advances its scenes, but the cape morph animation is incomplete.
  Special soundtrack selection beyond existing MDX sound events is also not
  established; do not hard-code `FinalCinematic.mp3` into generic client code.

## Save/load and limitations

Version 75 is required because `edict_t.ignore_alarm` is a serialized scalar.
The current round-trip suite checks that it survives a save/load cycle, and
older version 74 files are rejected according to `CONTRIBUTING.md`.

**Existing limitation (not solved by this change):** the WC3 save serializer
explicitly does **not** snapshot AI JASS VM roots, pending AI coroutines,
rosters or bot runtime (`save-load.md`). That means captain/planning settings
cannot be accurately resumed from a saved mid-battle AI script yet. This
change deliberately does not pretend that copying bot flags into `level_fields`
would provide AI script restoration. A complete bot save feature must serialize
or reconstruct each bot's VM and outstanding work alongside its captain data.

## Verification (not run in this patch preparation)

The game module tests cover alert suppression, alarm save round-trips,
captain logical teleport with empty and populated rosters, authored movement
flags under AI policy changes, and JASS registration/argument forwarding.
Client tests cover FIFO order and bounded overflow without loading retail
assets; full scene rendering and Escape still require interactive testing. The
cinematic JASS test verifies the original MDL asset path crosses the game/
client callback without translating it into a movie path.

Runtime acceptance with retail assets:

1. Start UndeadX08 with each named AI player; verify that the missing-native
   failures are gone and waves/defence continue. Inspect later errors too.
2. Attempt Naga water crossings and mixed ground/water attacks; ground-only
   units must not gain water access even after `SetAmphibious`.
3. Teleport the attack captain during formation and while attacking; verify
   membership and real positions are preserved, then observe arrival queries.
4. Finish UndeadX08 and check full-screen Arthas–Illidan playback, camera
   changes, sounds, Escape, and subsequent ending transition; repeat with
   `FFMPEG=0` and `FFMPEG=1` builds.
5. Compare the exact starting sequence, total length, soundtrack and blocking
   behaviour with an actual retail Warcraft III executable. MRF cape morphs
   are deliberately listed as a known outstanding renderer feature.

Follow [AGENTS.md](../../../AGENTS.md) and [CONTRIBUTING.md](../../../CONTRIBUTING.md)
for the normal build and test process. Compilation, game execution and unit
suite testing were intentionally left to the integrator for this patch.
