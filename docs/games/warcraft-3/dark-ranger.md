# Dark Ranger — Silence (`ANsi`, `ACsi`)

## Scope and authoritative data

Silence is the first Dark Ranger parity patch. `CAbilitySilence` lives in
`games/warcraft-3/game/skills/s_requested_abilities.c`; both `ANsi` and the
creep `ACsi` alias are registered in `s_skills.c`. The executed ability alias
selects the current AbilityData fields and presentation data. The MPQ asset
audit supplied for this work confirms the stock Dark Ranger `ANsi` uses:

| Field | Stock TFT values (levels 1/2/3) |
|---|---|
| `Area` | 200 / 275 / 350 |
| `Dur` | 16 / 20 / 24 seconds |
| `HeroDur` | 8 / 10 / 12 seconds |
| Mana / cooldown / cast range | 75 mana / 15 seconds / 900 |
| Ability `EffectArt` | `SilenceAreaBirth.mdl`, ground burst |
| Buff `BNsi.TargetArt` | `SilenceTarget.mdl`, `overhead` attachment |

The audit observed non-looping `Birth` (0–567 ms) and `Death` (1700–1900 ms)
for the area effect. The target model has non-looping `Birth` (333–667 ms),
looping `Stand` (1000–1333 ms), and non-looping `Death` (1667–2000 ms).
These sequence times describe authored model data, not hardcoded lifetimes in
OpenRealm. `Silence1.wav` is present in MPQ but its ability/sound alias binding
was not established; this patch does not invent a sound mapping.

## Runtime lifecycle

A successful point cast emits `WC3_EFFECT_EFFECT` at its selected point
*once*, independent of how many targets pass filtering. Generic effect
animation handles the point burst; no Silence-specific think callback or save
field is required.

Eligible units must be alive enemy targets inside the authored radius and pass
`S_SpellAllowsAreaTarget()`. This shared predicate handles authored air/ground,
organic/mechanical, spell immunity, and resistant/hero mask restrictions, and
must not apply the unit-target visibility check: an area spell can affect units
in fog. The selected rank's first `BuffID` supplies the timed status (stock
`BNsi`). Normal/Hero timing uses `S_SpellHeroDuration()`; classifying
level-6 creeps as Heroes for duration is **not** part of this patch.

`S_SpellApplyTimedStatusFromAbility()` retains the actual source ability and
rank for custom aliases. Successful applications are classified as negative
magical buffs and call `unit_status_enabletargetart(..., "overhead")`. The
resulting target art belongs to the status rawcode, not the caster or the
ability's one-shot burst. `G_SpawnStatusEffectTarget()` ensures only one live
model is tagged for an active buff and target incarnation. Refresh and
reapplication therefore do not pile up overhead models. Dispel, expiry, and
replacement route through `unit_removestatus()` and
`G_DestroyStatusEffectTarget()`, clearing the model's live status tag and
playing the authored Death animation where available.

The runtime does not synthesize an area effect when a custom map omits
`EffectArt`, or a target model when its buff omits art.

## Verification

`games/warcraft-3/game/tests/t_dark_ranger_silence.c` exercises the production
`S_CastPointTargetSpell()` entry point with non-stock Area=183, Dur=13,
HeroDur=4. It verifies target ownership, range, immunity, organic filtering,
source-ability identity, buff classification, exactly one ground effect,
exactly one overhead model per affected unit, refresh without duplicates,
dispel removal, and timed expiry. The source-controlled
`tests/resources-src/Units/NeutralAbilityFunc.txt` supplies test art paths.

To run after building with the regular test targets:

```sh
make test
```

Neither compilation nor tests were run by the patch author. Validate
appearance with actual retail MPQs in both TFT and RoC as needed; the fixture
models verify effect selection and ownership, not pixel-level MDX rendering.

## Remaining Dark Ranger work

Charm target restrictions and short visual, Black Arrow per-attack resource
and summon lifecycle, and Life Drain attached visuals/lightning/looping audio
are separate planned changes. See
[ability coverage](architecture/ability-coverage.md) and
[spell presentation](ability-and-item-effects.md).

## Charm (`ANch`, `ACch`) — Patch 2

Charm transfers an eligible unit to the caster's player permanently. The
standard `ANch` data is a single-rank, 150-mana, 45-second cooldown,
700-range spell; `DataA=5` is **maximum neutral-creep level**, not the
maximum level of a player-owned ordinary unit. The authored target mask is
`enemy,neutral,organic,nonhero,air,ground`. This patch leaves the common
mana, cooldown and range validation in the spell entry points and uses
`S_SpellAllowsTarget` for the authored mask and spell-immunity checks. The
existing Resistant Skin / Hero exclusion remains. A missing UnitBalance row
continues to be rejected rather than accepting an unknown target level.

A neutral unit's owner is identified by the map player type, including the
normal Neutral Hostile slot and any map-defined neutral slot. With authored
`DataA=5`, a level-6 neutral creep is rejected before spending mana, but a
level-9 unit owned by an ordinary enemy player is still eligible. The caster
and target must remain live enemies. These rules are also used for the
registered `ACch` alias (and share the Charm handler with applicable item
abilities), with the concrete ability code selecting the art and data.

### Ownership and presentation

A successful cast first cancels the target's existing combat, channel and
queued orders through `order_stop_cleanup`, clears the active order record,
and discards the old movement target. Unlike plain `stand()`, this
cannot start an old owner's next Shift order under the new owner. Then
`G_SetUnitPlayer` performs ownership and food accounting, which already
handles neutral-creep guard state and change-owner events. No new save field
or network contract is necessary.

Immediately after transfer, Charm emits one temporary `WC3_EFFECT_TARGET`
using the executing ability's authored `TargetArt`, attached at `overhead`.
The MPQ audit identifies `CharmTarget.mdl` (looping `Stand`, 0–1333 ms); the
existing generic **temporary** effect move frees the effect after one
animation sequence rather than retaining it as a persistent `BNch` status.
`MOVETYPE_LINK` and the target's spawn-generation guard keep the effect
attached to the original unit even when the unit moves or an entity slot is
reused. No Charm-specific thinker, duration constant, or made-up sound alias
is introduced. The precise retail effect lifetime remains observational
parity work.

### Verification

The production-cast tests in `game/tests/t_charm.c` use non-stock 37 mana
and a `DataA=5` cap. They exercise the neutral-vs-player distinction,
invalid dead/Hero/Resistant/magic-immune/mechanical targets, no mana on
failed casts, pre-transfer order cleanup, exactly one temporary overhead
model, no persistent Charm status, and effect retirement without undoing
ownership. `tests/resources-src/Units/NeutralAbilityFunc.txt` supplies
source-controlled test paths for `ANch` and `ACch` instead of relying on a
local MPQ installation.

Run the normal build and `make test` in the consuming repository; **the patch
author did not compile or run tests locally**. The effect test establishes
model selection, lifecycle and attachment ownership, not pixel-level
rendering. Further work for Dark Ranger is Black Arrow and Life Drain; visual
sound bindings, ribbon timing and unusual Charm ownership interactions
remain subject to retail verification.

## Black Arrow (`ANba`, `ACbk`) — Patch 3

Black Arrow is an **attack modifier**, not an indefinitely applied damage
status. Both the hero ability and the creep/melee alias resolve to
`CAbilityBlackArrow`; the command card uses `SPELL_TARGET_UNIT` for manual
targeting. Autocast is the unit's existing single selected autocast alias.
Manual targeting arms a one-shot attack status, just like the existing
Searing/Frost Arrow paths, without committing spell mana on click or walking.

At a committed weapon damage point, `S_BlackArrowPrepareAttack` resolves the
manual one-shot first and otherwise checks the selected Black Arrow autocast.
It checks ability ownership, attackability, the authored target mask, and
available mana; then consumes the one-shot marker and charges the alias's
`Cost` **once**. A rejected or unaffordable empowered shot remains an ordinary
attack. This works for the melee `ACbk` variant without requiring a ranged
weapon. No Black Arrow damage is conferred simply by learning the ability.

Ranged attacks select the ability's `MissileArt` at launch and override the
base weapon `Missilespeed` only when an authored positive value exists. The
stock `ANba` MPQ row uses `BlackArrowMissile.mdl` and speed 1050. The attack
missile component persists the selected concrete ability alias and level,
which are read at impact, even if autocast changes, mana runs out, or the
Hero learns a new rank while the missile is airborne. It inherits the
existing target-defense/armor-on-impact policy. Primary-hit resolution of
ordinary, missile-splash and artillery weapons accepts the same snapshot;
secondary splash damage never gains the Black Arrow bonus. One empowered
attack cannot simultaneously acquire Searing/Cold autocast effects at impact.
Existing Searing/Frost Arrow impact-selection behaviour remains outside this
patch's scope.

Patch 4 extends that launch-time selection into a post-mitigation timed
victim marker. An unempowered attack never creates a Dark Minion, even when
autocast remains enabled. The short-lived marker is created only after the
attack has survived shields, Spirit Link, invulnerability, miss and evasion,
and it persists when another unit lands the final blow. No unverified
conversion effect is synthesized.

`attackMissile_t` gains `arrow_ability` and `arrow_level`, both serialized
through the normal missile pool. The save format is bumped to 136 according to
repository save-compatibility policy; older saves are rejected. New tests
cover manual and autocast selection, authored cost and bonus, target masks,
a surviving launch snapshot, missile visuals/speed, and missile-field
round-tripping. Builds and test execution remain with the integrator.

## Black Arrow death-mark lifecycle — Patch 4

`S_BlackArrowMarkHit` runs for the **primary** empowered attack only, in
`T_DamageInternal` after positive post-shield/Spirit-Link unit damage is
confirmed, but before the damage can call `unit_die`. Its selected concrete
ability and rank were snapshotted by Patch 3. Ordinary attacks, misses,
secondary splash and spell damage do not mark a target.

A victim must be a non-Hero, non-building, non-illusion living unit that
produces raisable remains (`UnitData.deathType` Raise bit); scripted explosions
and already unraisable corpses cannot create Dark Minions. The target still
takes its Black Arrow-enhanced weapon damage if corpse generation is forbidden.
The marker uses its ability rank's `BuffID` when authored (fallback `BNba`
for older/partial game data), and `Dur` when positive, otherwise the established
original two-second window. It is a negative magical status and may be removed
by the ordinary buff-removal system.

The existing `unit_statusdeath` dispatcher delivers `A_STATUS_DEATH` to the
mark's originating `CAbilityBlackArrow` procedure. The procedure consumes the
mark before spawning, verifies the source incarnation with
`S_SpellStatusSource`, and uses the recorded rank's `UnitID` and `DataC`
for the Dark Minion type and timed life. Where an ability alias leaves those
fields blank, it inherits its base ability's fields. The newest active Black
Arrow marker wins if different custom buff rawcodes coexist. `S_SummonAbilityAt`
handles ownership, generated unit events, and ordinary timed-life disposal.
On a successful conversion the original remains are marked unraisable and
removed after their normal Death presentation; failed summons leave corpse
policy unchanged. The original killer need not be the Dark Ranger; expiring, dispelling or
invalidating the marker prevents the summon. A target dying under an active
mark summons at most once. There is no ability-specific scan of corpse
entities or new serialized gameplay state.

`games/warcraft-3/game/tests/t_dark_ranger_black_arrow.c` now covers lethal
and delayed deaths, marks expired before death, non-corpse targets, rank
refresh, invulnerability, stale caster incarnation and mark save/load.
Its stock-shaped object references use locally configured hfoo/hpea summons
as deterministic stand-ins for MPQ-only Dark Minion unit IDs. The actual
`ndr1`/`ndr2`/`ndr3` scale, art and combat data remain MPQ-authored.

**Visual parity not included:** a distinct skeleton-creation effect has not
been established by the provided MPQ audit. Patch 5 remains Life Drain
attachments, continuous lightning, and loop sound; subsequent parity work
may revisit the Black Arrow death presentation. No builds or tests were run
by the patch author.

## Life Drain presentation — Patch 5

`ANdr` executes as `CAbilityDrainNeutral`, sharing the existing `AHdr` health/
mana channel thinker without inheriting Siphon Mana's 200% mana cap. Its
presentation now begins when the production unit-target cast creates that
thinker:

- The stock caster buff class `Bdcl` resolves `DrainCaster.mdl`, attached at
  `chest` to the Dark Ranger. The victim class `Bdtl` resolves
  `DrainTarget.mdl`, also attached at `chest`. Both effects are owned by the
  channel thinker and render their authored looping `Stand` sequence.
- `ANdr.LightningEffect` lists `DRAB,DRAL,DRAM`. A health-only drain uses
  index 1 (`DRAL`, DrainLightning texture); a mixed health/mana transfer uses
  index 0, and a mana-only transfer index 2. The ability alias's resolved
  function table supplies these IDs. The generic attached lightning API
  updates both endpoints from live unit bodies every frame. Its current
  half-radius endpoint offset is **not** proven to equal the retail chest
  socket; exact chest-socket lightning endpoints remain parity work.
- `ANdr.Effectsoundlooped=DrainLoop` resolves through the authored ability
  sound table (stock `LifeDrain.wav`) and plays on the channel thinker. This
  is a loop, not a fabricated one-shot cast sound.

The beam is an **indefinite** lightning slot while the channel is live:
`LightningData`'s duration of 2 describes authored beam animation data, not
an 8-second channel timeout. The thinker's already-persisted `variation`
field stores the unique registry handle, which is saved alongside the
lightning registry and moving endpoint entity identities. On Stop, stun,
movement, caster/target death, range/target invalidation, completion or
replacement cast, its next channel tick removes only the matching beam
handle, turns off sound and retires both owned attached effects. This avoids
old thinkers deleting a new cast's beam after a quick recast; no new save or
network fields or version bump are necessary.

The attached model presentation and beam use only game-module-authored
entities/lightning; the universal client remains unaware of Dark Ranger.
Missing models, sound rows or lightning declarations are allowed and do not
change health drain gameplay. Siphon Mana visuals are not part of this patch.

`game/tests/t_ability_lifecycle.c` covers the real `ANdr` cast, both model
attachments, `DRAL` selection and moving endpoints, loop sound selection,
Stop cleanup, old-channel recast isolation and save/load followed by cleanup.
The test archive includes `NeutralAbilityFunc.txt` spell/buff art and a
`DrainLoop` alias in `AbilitySounds.slk`; it does not require retail MPQs.
No local build or tests were performed by the patch author.

**Remaining uncertain retail details:** the exact internal choice of
`DRAB` for mixed drains, lightning socket offsets, MDX emitter timing, and
whether additional ability aliases use other buff classes. The MPQ audit
confirms the stock Life Drain files but does not resolve those runtime rules.
