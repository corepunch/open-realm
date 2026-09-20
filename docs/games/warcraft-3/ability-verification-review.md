# Ability verification and lifecycle fixes

## Scope and evidence

The September 2026 review at `41602e54` inspected all 39 Warcraft III
`game/skills/*.c` files. Three agents covered combat spells, orders/economy,
and passives/items/campaign handlers; the coordinator covered shared dispatch.
The original baseline passed 1,132 WC3 tests in each archive mode, but sixteen
new behavior scenarios failed and enabling Human autocast crashed. Registry
presence and status-record creation had overstated working functionality.

The fixes retain these reproductions as permanent tests, extend their inverse
and lifecycle coverage, and correct additional failures exposed by that work.
The four new suites contain 37 tests / 302 assertions, passing in both ROC and
TFT modes. Five save-schema tests additionally cover the new channel fields.
This verifies the named contracts, not exhaustive retail parity or rendered art.

## Fixed contracts

| Area | Previous failure | Owning fix and regression evidence |
|---|---|---|
| Human autocast | A boolean message was dereferenced as a target pointer; aliases used base data | Human, validated-spell, Avatar and bolt procedures decode only the current message's union member. The command/HUD/scheduler retain the actual rawcode. Alias cost/heal, runtime membership, switching, removal and unsupported-policy rollback are tested. |
| Repair autocast | Switching related procedures could clear their shared policy flag | Disable the old policy before enabling its replacement; disabling a nonselected alias has no effect. Rejected enable restores the previous policy. |
| Hold Position | Out-of-range acquisition walked toward enemies | Attack returns to stationary scanning; an in-range enemy can still be attacked. |
| Delayed damage | An old projectile kill replaced a newer Move or consumed its queue | Combat completion requires the current Attack procedure and matching target. Animationless post-hit fallback also requires unchanged move and target. |
| Explicit Attack/Smart | Replaced Follow/Patrol/Attack-move resumed after combat | `S_OrderAttack` clears retained movement only after accepting an explicit order. Autonomous acquisition keeps continuation; queued orders replace it only when executed. |
| Repair completion | `building->stand` erased the producer queue | Repair owns the worker's completion only; the target keeps its production behavior and queue. Tests advance the retained trainee. |
| Flame Strike | Damage was treated as a long cast delay; pulses ran every frame | Read Cast/HeroDur/Dur and Hfs1–Hfs6 correctly; preserve absolute fractional pulse deadlines, phases, building multiplier and aggregate cap. |
| Blizzard | Zero Dur truncated six authored waves to two | Wave count and authored interval determine completion, independently of the zero-duration sentinel. |
| Life Drain / Siphon Mana | Life Drain affected mana; channels survived cancellation | Separate authored health/mana fields and allied transfer; validate each cast, tether and entity incarnation at every pulse. |
| Channel lifecycle | No-target casts lacked state; old thinkers survived recasts or stopped new orders | Start all channel shapes consistently; capture a cast serial and owner/target incarnation; route move-leave to the active procedure and explicitly cancel on Stop. Stun, movement, recast, expiry, reuse and save continuation are tested. |
| Aerial Shackles | The caster buff was applied to the victim, so movement/attacks remained allowed | Read the target member of the authored buff pair. Remove its lock when that channel ends, preserving a replacement cast's lock. |
| Storm Bolt | Spell immunity blocked damage but not stun; creep level selected Hero duration | Check impact acceptance before applying stun and use actual Hero identity for HeroDur. |
| Frost Nova | A no-target burst damaged around the caster | Register a unit target; apply direct DataB damage and area DataA damage around that target. |
| Resurrection | Only Heroes were eligible | The no-target spell finds ordinary friendly corpses in the authored area/count, reuses their edicts, retires death state and restores unit/food activity. Reject an empty cast before committing resources. |
| Attribute items | Strength, Agility and Intelligence were rotated | Both permanent tomes and passive attribute aliases use DataA=Agility, DataB=Intelligence, DataC=Strength. |
| Passive items | Exact base-code comparisons discarded stock alias bonuses | `A_ITEM_ADD` / `A_ITEM_REMOVE` carry `abilityitem_t` to the owning passive procedure. Read each alias's amount on pickup/drop; no per-family global amount cache. Stacked items survive save/load and independent removal. |
| Natural creep sleep | Wake removed unrelated owned overlays | Tag sleep art with ACsp and require that identity during cleanup. Wake, move and disable preserve regeneration art. |

## Data and timing

Use the archive audit instead of inferring fields from tooltip prose:

```sh
build/bin/ability_audit -data 'data/Warcraft III' -roc -raw AIsm
build/bin/ability_audit -data 'data/Warcraft III' -tft -raw AHfs
python3 tools/wc3_ability_class_audit.py --format=coverage
```

The active TFT Flame Strike row uses Hfs1/DataA for full pulse damage,
Hfs2/DataB for its interval, Hfs3/DataC for lesser pulse damage, Hfs4/DataD
for its interval, Hfs5/DataE for the building multiplier, and Hfs6/DataF
for the aggregate damage cap. Cast is the initial delay; HeroDur supplies
the full-damage phase and Dur the damage lifetime. Deadlines advance from
the previous scheduled pulse, so a 0.33-second interval does not become
0.4 seconds on every 100 ms server tick. Positive sub-frame intervals are
caught up at the next simulation step; invalid intervals are rejected/logged.

Both ROC and TFT archive audits confirm AItg → AIat (attack bonus 1),
AId1 → AIde (armor 1), and the three tome attribute columns. Combat fixtures
include an explicit ROC numbered Data11/Data12 conversion test for Blizzard.
Running TFT numeric fixtures in ROC mode verifies runtime compatibility;
it does not independently verify every ROC authored value or TFT-only spell.

## Dispatch and persistence

`abilityCall_t` is a message-tagged union. `A_COMMAND` supplies a client,
`A_INIT` a classname, `A_AUTOCAST_SET` a boolean, and only target-bearing
messages supply a `spellTarget_t`. Never eagerly read the target union member
before deciding which message is being handled.

`edict.channel.code` is nonzero on the caster only. Each channel thinker
retains its spell rawcode in `class_id`, the cast `channel.serial`, and
`owner_spawn_time` / `target_spawn_time` where applicable. Reused owner/target
slots and replacement casts cannot inherit an old effect. `S_SpellEndChannel`
clears only a matching cast, then releases its thinker; cancellation does not
install Stand over an already accepted replacement order.

Save format **21** adds this channel identity state to the edict schema.
Older save versions are rejected. The new channel callbacks are appended to
`save_cfunctions[]`; never reorder existing callback indexes. The saved
channel origin is preserved for movement cancellation after loading. A live
drain and stacked passive item aliases have production WriteGame/ReadGame
continuation tests, in addition to the individual schema tests.

## Tests

```sh
make test-wc3-engine WC3_PATTERN='wc3_ability_*'
make test-wc3-engine WC3_PATTERN='wc3_order_lifecycle.*'
make test-wc3-engine WC3_PATTERN='wc3_item_lifecycle.*'
make test-wc3-engine WC3_PATTERN='wc3_save.*'
make test
make build
```

Permanent regression sources are `game/tests/t_ability_dispatch.c`,
`t_ability_lifecycle.c`, `t_order_lifecycle.c`, and `t_item_lifecycle.c`.
Existing combat, building, spell and save tests also check affected paths.
Final verification on 2026-09-13: `make test` passes 3,228 test invocations /
65,585 assertions across 20 suite invocations, including 1,174 WC3 tests /
25,316 assertions per archive mode. `make build` passes. Both builds emit no
compiler warnings; registry coverage remains 193 registered TFT class IDs.
Use full local socket access for the aggregate suite's UDP tests; sandbox
bind denial is not a gameplay failure. No interactive game launch was needed.

## Limits and history

The fixes address reproduced defects and their related lifecycle cases.
Previously documented partial implementations remain partial: Wisp/Acolyte
harvesting, mine transformations, general transport drop, Purchase Item,
Mirror Image's complete illusion semantics, several status-only spells,
and Siphon Mana's temporary above-maximum mana/decay behavior. Visual effects,
full target-mask vocabulary and comprehensive retail spell parity remain
separate work. Do not interpret a registered procedure as complete behavior.

Blame traced the unsafe Human union reads to `e12b63ad0`, Flame/Drain field
and timer errors to `22ff3692`, item alias/attribute errors to `85fd0906`,
attack continuation to `317fc09ae`, Repair completion to `6d5025e92`, and sleep
overlay cleanup to `ebbe30e2`. These histories explain why the new regressions
exercise caller/event order, authored aliases and inverse behavior rather
than only testing procedure lookup.

See [ability coverage](architecture/ability-coverage.md),
[ability implementation](ability-implementation-plan.md),
[save/load](save-load.md), and [autocast](autocast.md).

## Jaina / Archmage follow-up (September 19, 2026)

The Archmage review tightened three abilities without adding Hero-specific
branches. `AHwe` now treats DataA as the authored summon count, records the
creating ability on each summon, and emits TargetArt while retaining the shared
timed-life/event path. `AHbz` now evaluates its own authored target mask, applies
DataD to structures after the DataF per-wave cap, and emits DataC EffectArt
shards without using the visual positions for hit detection.

`AHmt` now participates in the shared channel lifecycle instead of relocating
immediately. DataB owns the completion delay; a thinker retains the destination
entity incarnation, temporarily pauses that destination, owns persistent area
effects, and cleans all of those resources on success or cancellation. Completion
uses the current destination position, keeps the caster inside the DataA total,
excludes allied-player armies and structures from the payload, honors DataC
clustering, and uses ordinary unstuck placement. The thinker callback is appended
to the save callback roster, and regression coverage includes delayed completion,
unit-cap/ownership filtering, structure destinations, destination death,
cancellation cleanup, Water Elemental DataA ownership, Blizzard structure
scaling, and production save/load continuation.

Brilliance Aura remains on the existing cached Hero-aura path. This follow-up
does not replace that path with the newer generic target-token helper because the
current unit `targtype` representation conflates movement class and
organic/mechanical classification; doing so here could regress stock mechanical
recipients. That broader target-model cleanup remains separate work.

### Jaina / Archmage remaining-fidelity follow-up (September 19, 2026)

A second conservative pass closes behavior that the bundled Warsmash source
makes explicit without broadening the shared damage/type model. Blizzard now
waits for its authored `Cast` interval before the first shard wave, then splits
each wave into shard presentation followed by damage 0.8 seconds later. The
phase is stored on the already-serialized thinker state, so saving between the
shards and impact resumes the pending damage phase rather than replaying art.
The existing target mask, DataD building multiplier and DataF aggregate cap
remain authoritative. Cold/spell damage typing and spell-sound parity are still
separate shared-system work.

Brilliance Aura now consumes the stock `air,ground,friend,self` target mask in
the cached Hero-aura path rather than treating every friendly entity in range as
a recipient. Rows that omit `targs` keep the historical friendly fallback for
sparse ROC/custom fixtures. This deliberately does not rewrite the engine-wide
organic/mechanical classification model; that broader cleanup is still separate.

Water Elemental keeps its existing DataA/UnitID/timed-life path but now preserves
the caster facing and relinks collision-displaced summons. Mass Teleport likewise
relinks every relocated unit (and dirties FOW blocker state where relevant), so
the server broad phase no longer retains the source position after the visual and
simulation coordinates have moved. Regression coverage documents Blizzard's
0.8-second phase timing and save/load continuation, Brilliance target filtering,
Water Elemental separation/facing, and Mass Teleport vacated-space relinking.

### Jaina / Archmage data-driven aura and targeting follow-up (September 19, 2026)

A third conservative pass closes two data-driven gaps that are explicit in the
bundled Warsmash implementation. Brilliance Aura now treats DataB as its
flat-versus-percentage switch: flat DataA remains MP/sec, while percentage DataA
scales the recipient's intrinsic mana regeneration (base unit regeneration,
ordinary mana-regeneration bonuses, and the current Intelligence regeneration
term). Flat and percentage Brilliance providers keep separate strongest-copy
slots, matching the separate non-stacking stat families used by Warsmash. The
Hero-aura cache also resolves the actual authored alias and rank before reading
Area, `targs`, DataA, and DataB, so custom Brilliance-derived abilities no longer
silently fall back to the stock `AHab` row.

The shared unit-target validator now reads `targs` from the caster's current
ability rank instead of always using level 1. This matters to Blizzard and Mass
Teleport when custom object data changes target classes across ranks, and it
preserves the requested alias as the data source. Focused regressions cover
rank-specific target masks and a custom percentage-mode Brilliance alias mixed
with a stock flat provider.

This pass still does not invent cold/spell damage typing, per-shard sound
semantics, or an engine-wide organic/mechanical target-class rewrite. Those
remain shared-system fidelity work because the current OpenRealm representations
do not model them cleanly enough for a low-risk Jaina-specific change.

### Jaina / Archmage summon identity and Blizzard effect-object follow-up (September 19, 2026)

A fourth conservative pass closes two presentation/lifecycle details that the
bundled Warsmash implementation makes explicit without extending OpenRealm's
shared damage model. Summon Water Elemental now also applies the ability's
authored BuffID to each created Elemental. OpenRealm deliberately keeps `BTLF`
as the authoritative timed-life clock because the existing timed-life bar and
`UnitPauseTimedLife` native are built around that status; the authored Water
Elemental buff supplies the WC3 ability-specific status/presentation identity
without introducing a second expiration timer. `BTLF` is now treated as
non-dispellable lifecycle state by the shared Dispel/Purge filter, so dispel
still deals its authored damage to summoned units but cannot accidentally turn
a surviving timed summon into a permanent unit.

Blizzard now resolves each shard's EffectArt through the level's authored
`EfctID` object, matching Warsmash's `effectId` path, instead of resolving the
visual directly from the casting ability alias. Focused regressions cover the
Water Elemental BuffID plus retained `BTLF`, preservation of timed life after
Dispel Magic, and the Blizzard EfctID art lookup.

That fourth pass intentionally left cold/spell damage typing and per-shard
ability sound alone because both required shared-system support. The following
pass adds the sound-data path; typed SPELLS/COLD damage still remains separate.

### Jaina / Archmage ability-sound follow-up (September 19, 2026)

A fifth conservative pass closes Blizzard's remaining high-confidence sound-data
gap without changing spell damage semantics. The typed metadata registry now loads
`UI\SoundInfo\AbilitySounds.slk` with the same sound-row schema already used for
unit/UI sounds, and ability/buff presentation exposes authored `Effectsound` /
`Effectsoundlooped` fields. `G_PlayAbilityEffectSound` resolves an alias from the
requested object or its base object and emits a positional world sound at the
requested effect point using the authored volume.

Blizzard now uses the level `EfctID` for both shard art and shard sound, matching
the bundled Warsmash implementation's `spawnSpellEffectOnPoint(effectId)` plus
`unitSoundEffectEvent(caster, effectId)` sequence. The sound fires once for each
DataC shard during the presentation phase, never during the delayed damage phase.
Regression coverage injects a synthetic `AbilitySounds.slk` row and verifies six
shard sounds, alias/path resolution, positioned playback, and 0-127 volume scaling.
The test MPQ also carries a minimal AbilitySounds fixture and verifies it is packed.

The server intentionally selects the first authored ability-sound file rather than
calling `rand()`: Warsmash performs variant selection in rendering/audio code, while
using OpenRealm's simulation RNG for presentation would perturb deterministic game
state. Client-side random variant selection plus authored pitch/pitch-variance are
still presentation refinements. SPELLS/COLD Blizzard damage typing, the broader
organic/mechanical target model, and finer Mass Teleport placement/order parity
remain separate shared-system work.
