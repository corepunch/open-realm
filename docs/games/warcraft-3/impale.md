# Impale — travelling wave, stun, airborne lift and effect presentation, targeting and lifecycle (patches 1–4)

Impale (`AUim`) and the independently authored creep ability (`ACmp`) run through
`CAbilityImpale` in `skills/s_impale.c`. Patch 1 replaced the old instantaneous
line-wide hit scan. Patch 2 adds the stun and airborne lift lifecycles. Patch 3 adds caster,
travelling miss, and BUim hit-model presentation. Patch 4 adds DataE/DataF
interruptibility and airborne vulnerability plus the unit-spell target lock.
Retail-accurate spike spacing and lift motion remain future work.

## Game-data contract

- `DataA` gives wave travel distance; `DataB` gives the total travel time;
  `DataC` gives damage at each successful hit.
- `Area` is provisionally treated as the **full width**, so half the value
  is applied on either side of the line, extended by target collision radius.
  The precise retail width convention still requires verification.
- `targs` is evaluated by the shared `S_SpellAllowsAreaTarget` filter,
  in addition to the enemy/alive checks. A cast does not require visibility
  of every unit subsequently struck by its travelling area effect.
- `BuffID` (`BUim` if missing), `Dur` and `HeroDur` establish a timed stun
  when each target is struck. `BUim` contributes `WC3_STATUS_STATE_STUNNED`;
  the state is evaluated by the shared movement eligibility check, AI and spell execution independently
  of the unit's currently playing animation.
- `DataD` determines the separate air-time duration for ground and hero units.
  Landing does not end stun, and a shorter custom stun does not prematurely
  end the airborne presentation.
- `DataE` (Uninterruptible, introduced in 1.32.6) determines whether movement
  by the original caster cancels the pending wave; legacy data without this
  column is interruptible. It does **not** change the ordinary cast-point or
  unit animation scheduler.
- `DataF` (Airborne Units Vulnerable, introduced in 1.32.6) controls physical
  attack admission and damage while a victim is airborne. Legacy rows without
  the column prevent both. New unit-targeted spells remain blocked in either
  mode until the victim lands; area effects may still damage a vulnerable
  airborne unit. The editor's source fields are read by the same `AB_Data`
  ROC/TFT resolver used for other authored data, including custom aliases.

## Simulation ownership and timing

The non-rendered wave thinker has one `impaleWave_t` optional component
(`COMPONENT_IMPALE_WAVE`). It snapshots cast origin, direction, damage, width,
travel parameters, ability/rank and the caster's spawn identity. Its previous
travel distance is saved so skipped simulation frames cannot cause missed hits.
Distance travelled is `DataA * clamp(elapsed / DataB, 0, 1)`; the thinker
resolves the finite swept interval between its last and new distances. Targets
are processed by projected distance and stable edict index for exact ties.

An invisible `ImpH` identity edict tracks each hit with the wave pointer,
wave incarnation timestamp, victim pointer and victim incarnation timestamp.
Markers are retired when their wave finishes, its caster dies or its original
caster slot is recycled. Legacy DataE=0 waves also terminate when the caster
moves more than 0.5 world units from the snapshotted cast origin, while
DataE=1 waves continue unaffected by movement. Multiple waves can hit the
same target independently; one wave cannot hit it again on later simulation frames.

The wave remains authoritative if its visual assets are missing. It does not
use a projectile model and is independent of camera, frame rate and animation
completion. The victim's `BUim` status owns stun while `COMPONENT_IMPALE_LIFT` (ID 32)
owns airborne presentation. Unlike ordinary Cyclone, Impale does **not**
reclassify the victim as `TARG_AIR`. During its `DataD` interval, the
`impaleLift_t.vulnerable` snapshot from `DataF` determines whether the common
weapon acquisition and `T_DamageInternal` paths can affect it. Regardless of
that flag, the shared unit-targeted spell gate blocks **new** targeted casts
until the airborne phase ends. It never changes actual unit spell immunity,
attack category, ownership, or permanently applied invulnerability flags.
Reapplying a lift while airborne preserves its height offset and selects the
new hit's DataF policy for the remainder of the new lift.

The lifted position is `terrain/support + FlyHeight + S_ImpaleLiftOffset`.
This is important: flight, Raven Form, Ensnare and other lift owners continue
using their existing `FlyHeight` value; Impale never snapshots and restores
that mutable value. The temporary offset is computed from saved simulation
deadlines (`start_ms`, `end_ms`), peak and previous offset. A second Impale
restarts the curve from the current offset without a snap to ground.
`G_RunEntity` updates the lift even when `monster_think` skips movement while
stunned. A unit's death immediately retires the lift. Landing never wipes
any remaining BUim or independent stun status.

**Lift visual caveat:** The sinusoidal curve and 120-unit peak are isolated,
non-authoritative presentation approximations. `DataD` is authoritative for
its duration; the available asset report does not establish retail peak,
trajectory or any precise spike animation keys.

## Authored models and the wave's presentation

The local asset audit identifies `ImpaleCaster.mdx` as a non-looping 1.4s
Birth model, `ImpaleMissTarget.mdx` as the ability's SpecialArt (1.4s Birth),
and `ImpaleHitTarget.mdx` as `BUim`'s TargetArt (about 1.367s Birth).
`ImpaleTarget.mdx` was not present in the inspected archives. No asset was
fabricated to replace it. `ImpaleTargetDust.mdx` has an SPNxCLID event and
resolves its child via the existing MDX spawn-event system.

- A successful `AUim`/`ACmp` cast spawns `CasterArt` from the **concrete**
  ability row as a temporary model linked to the caster. The cast and its
  damage wave do not depend on animation completion or successful loading.
- After every simulated sweep, a sample grid along the authored direction
  spawns the concrete ability's temporary `SpecialArt` at otherwise-empty
  ground positions. Existing authoritative hit markers (which snapshot the impact position)
  replace a nearby miss sample with a hit effect. The 64 world-unit visual sampling spacing
  is a *provisional presentation choice*; it is not proven by asset tracks or
  retail motion. Samples are recovered from serialized previous wave distance,
  avoiding a second persistent visual clock and duplicate spikes after load.
  A 32-sample-per-tick cap limits presentation entity pressure on malformed or
  extremely large custom ability waves.
- On each recorded hit, `BUim` or the authored `BuffID` supplies the
  one-shot `TargetArt`. The model is anchored to the ground at the impact
  coordinate rather than attached to the victim, which rises independently.
  A lethal hit may still play its model, and a failed visual allocation never
  suppresses the hit, status or lift.
- MDX event identifiers `AUIH` (ImpaleLand), `AUIT` (ImpaleHit) and `AUIM`
  (Impale) are resolved by the existing renderer/model sound-event path.
  Do not call the same sounds explicitly from the spell simulation. The
  event timestamps and exact model/texture track transforms were not
  established by the available asset audit.
- The temporary `Birth` model system owns visual end-of-life. The gameplay
  thinker does not retain effect pointers or force-end models when the wave
  finishes. In particular the 1.4-second effect lifetime is independent of
  the roughly 0.3-second stock wave travel time.

The low-confidence visual questions remain the real spike spacing, sequence
selection on the Crypt Lord himself, exact ground spike placement and the
missing `ImpaleTarget.mdx` asset. The hit/miss art choices, authored identities
and sound event identifiers are supported by the inspected data. Test
fixtures use generated `TestUI/Models` assets rather than a developer's
Warcraft MPQ files.

Status-slot exhaustion prevents creating BUim, but the physical hit and
independent lift still occur. A custom zero-duration stun similarly does not
prevent an authored nonzero `DataD` lift. Neither situation corrupts another
status owner.

## Persistence and regression coverage

`G_COMPONENTS`, the save component schema and the C callback roster include
the wave (save version 129) and the independent victim lift component
(save version **130**). Old saves are rejected, as the repository requires. `tests/t_impale.c` uses synthetic data for both `AUim` and `ACmp`,
with a deliberately nonstock 73/37 damage split, several sweep deadlines,
wrong-relationship/wrong-type units, and stale caster ownership.
The save round-trip regressions in `tests/t_game.c` verify wave callback and
hit-marker identity, plus victim lift deadlines, height continuity and
remaining stun after loading. The additional production-path cases in
`tests/t_impale.c` exercise launch, stun, landing, existing FlyHeight, overlapping
independent stun, repeated wave impact and victim death. Patch 3 checks the
cast/hit/miss model identities, sweep progression, ground attachment,
and `ACmp` art selection. No new save contract or callback was introduced
by patch 3 because the wave's previous distance already persists.

Build and execution of tests are intentionally delegated to the user.

## Patch 4 targetability and compatibility coverage

Blizzard's [Warcraft III 1.32.6 notes](https://news.blizzard.com/en-gb/article/23445051/warcraft-iii-reforged-patch-notes-version-1-32-6)
explicitly introduce *Uninterruptible* and *Airborne Units Vulnerable*. Unlike
Cyclone, Impale's airborne phase must not permanently reclassify units. The
common attack-order target check and damage recipient gate enforce DataF;
unit-targeted spell casts reject victims still in the air, even if DataF=1.
The regular area target filter may include airborne victims only when DataF=1.

Tests exercise a legacy row without DataE/F, a modern row with both fields,
a moving caster, a saved airborne victim and the same target immediately after
landing. **Known limitations:** exact retail overlap semantics for two distinct
Impales, whether other spell effects can bypass the airborne target lock, and
complete old-client cast-animation interruption behaviour still require retail
traces. The current DataE movement rule intentionally affects only the
already-launched wave, not engine animations or arbitrary scripted order changes.
