# Beastmaster Stampede (`ANst` / `Arsp`)

## Data contract

The standard Beastmaster ultimate (`ANst`) and Rexxar campaign variant
(`Arsp`) share the `ANst` base code but must read **their own** AbilityData
rows and current rank. Both are point-targeted *channels*, not summons.

| Authored field | Consumer |
|---|---|
| `DataA` | Lizards emitted per second (stock: 2) |
| `DataB` | Collision radius (stock ANst rank 1: 55) |
| `DataC` | Damage per lizard (stock: 60; Arsp rank 1: 80, rank 2: 130) |
| `DataD` | Explosion damage radius (stock: 275) |
| `DataE` | Collision-to-explosion delay (stock: 0.2 s) |
| `Area` | Length of the spawn line and forward travel distance (stock: 1000) |
| `Range` | Maximum distance from caster to selected start point (stock: 300) |
| `Dur` | Length of emission, not lizard lifetime (stock: 30 s) |
| `MissileArt`, `Missilespeed` | `StampedeMissile.mdl`, 500 world units/sec |
| `SpecialArt` | `StampedeMissileDeath.mdl` on impact |
| `Effectsound`, `Effectsoundlooped` | `StampedeCast`, `StampedeLoop` |

`ANst` authoring has extra fallback rows beyond its exposed rank 1; `Arsp`
has two exposed ranks. Do not infer rank from a base-code-only lookup.

## Lifecycle

The shared casting pipeline charges mana, starts cooldown and owns the active
channel. `stampede_channel_think` emits lizards at the `DataA` cadence while
`S_SpellChannelActive()` remains true and the authored duration has not expired.
The spawn line is perpendicular to the direction from caster to selected point;
its length and each projectile's travel distance are the authored `Area`.
The per-channel deterministic PRNG seed is persisted in the channel thinker,
so save/load does not reshuffle emission or change the number of lizards.

Each lizard is a modelled, fixed-target server projectile with a captured
caster generation. The existing `g_phys.c` path owns its travel and looping
`Stand` animation. A per-frame segment sweep catches first contact with
eligible non-air enemy/neutral units and authored debris. On contact, the
projectile stops moving; after `DataE`, the blast damages all eligible units
and debris inside `DataD` **once** and presents `SpecialArt`. A lizard that
reaches the end of its travel without contact disappears harmlessly.

Stopping, moving, stunning, silencing or killing the caster ends *future
emission* and the loop sound. Already-released lizards retain their snapshot
caster-generation identity and complete their flight if that entity still
exists; a recycled caster slot cannot receive damage attribution.

All state lives in existing saved edict/channel fields. The emitted lizard's
`prethink` sweep, delayed explosion `think`, and emitter `think` are listed
in `g_save.c`; no network payload or persistent component type is required.
Save version 123 reflects the new callback identities.

## Evidence and remaining retail comparisons

This implements the MPQ/SLK/function file contract reported in the November
2026 Stampede audit, and the independently described editor behaviour that
`Area` controls the spawn-line length **and** travel distance:
<https://github.com/Cokemonkey11/wc3-ability-doc/blob/main/sources/screwthetrees-wc3-editor-ability-insight-document/2023-11-14.md>.

**Not proven to be exact retail:** the spawn distribution (a deterministic
uniform pseudo-random offset is currently used), the exact frame of the first
beast, whether retail allows explosions on travel expiry, how contact accounts
for the unit's collision radius, and whether released lizards survive every
possible interruption. Compare those with stock Warcraft III before claiming
full visual/timing parity.

## Focused tests

- `wc3_spell.stampede_channel_emits_by_data_a_and_cleans_up_after_cancel`
  checks emission cadence, no generic periodic radial damage, and interruption.
- `wc3_spell.stampede_each_beast_explodes_once_at_its_saved_impact` checks
  Rexxar-specific damage and excludes air, allied and out-of-range units.
- `wc3_save.stampede_collision_delay_and_explosion_resume_from_save` saves a
  pending `DataE` blast, restores callback identity and verifies exactly one hit.
- Manual: verify `ANst` / `Arsp` projectile visuals, blast, death, debris,
  looping sound interruption, and in-flight save/load on a retail map.
