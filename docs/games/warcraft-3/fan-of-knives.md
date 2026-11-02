# Fan of Knives (`AEfk`): Target Cap and Enhanced Physical Damage

## Ownership and data

The Night Elf Warden ability uses `CAbilityFanOfKnives` in
`games/warcraft-3/game/skills/s_warden_abilities.c`, entered through the
shared no-target `S_CastNoTargetSpell` pipeline. Its concrete `AbilityData`
row (including custom map overrides and aliases with `code=AEfk`) owns:

- `DataA` (`Efk1`): nominal damage per target.
- `DataB` (`Efk2`): maximum total *nominal* damage, before defense-type
  mitigation. A nonpositive DataB remains the existing unlimited sentinel.
- `Area`: cast-centered target radius; `targs`: target classes.

Reforged 1.36.2 changed nominal damage to 70/130/200 and maximum damage to
420/780/1200, for six victims at each level; earlier archives have different
DataA/DataB values. Read the installed SLK/map overrides, never those constants.
Blizzard's patch notes explicitly call DataB a *unit cap* change:
https://us.forums.blizzard.com/en/warcraft3/t/136221146-ptr-3-patch-notes/31943

## Target cap

The previous implementation counted all eligible targets and spread DataB over
all of them, making every victim receive reduced damage when the limit was
exceeded. The new behavior keeps full-strength hits for at most
`floor(DataB / DataA)` victims, rather than dividing DataB among every enemy.
Targets filtered out by the authored mask, spell immunity, invulnerability or
Ethereal state do not consume cap capacity. For custom non-multiple caps, the
remainder cannot purchase another full hit (e.g. 110/51 permits two hits).

The current implementation visits targets in stable edict order to avoid adding
unsynchronized random selection or a new persistent allocator. **Open retail
question:** which specific eligible units are chosen in over-cap clusters, and
whether custom fractional caps allow a partial final hit. Compare a controlled
seven-/eight-unit retail cast before claiming exact victim-selection parity.

## Damage classification

Fan of Knives is a **magical ability** dealing **enhanced-physical damage**.
Its attack type is `ATK_SPELLS`, so use the active gameplay-constants
Spells-vs-defense multiplier (0.70 against Hero defense in the stock fallback).
Its damage type is Enhanced, which **ignores numeric armor**, unlike ordinary
physical weapon attacks. It cannot damage Ethereal units and its magical
ability targeting still rejects spell immunity. In contrast to magical spell
damage, it does not receive the 1.66 Ethereal amplification and does not spend
a `Bam2` Anti-Magic Shell spell-damage absorption pool.

The Warden implementation uses `G_AttackTypeDamageMultiplier()` to reuse the
same active table as weapon calculations and calls the shared non-primary
`T_Damage` pipeline for health, damage events, death and attribution. It does
not call `S_SpellDamage`, `S_ResolveAttackHit`, or `G_AttackDamage` (which would
apply ordinary armor). Other spells' `S_SpellDamage` behavior is unchanged.

Damage type semantics reference: https://www.hiveworkshop.com/threads/wc3s-damage-system.279673/

## Cast presentation (FanOfKnivesCaster / FanOfKnivesMissile)

The TFT archive has two distinct animated models under
`Abilities\Spells\NightElf\FanOfKnives\`:

- `FanOfKnivesCaster.mdx` (33,264 bytes) is a cast-centred burst.
- `FanOfKnivesMissile.mdx` (17,236 bytes) supplies an animated knife burst;
  its name does not imply independent simulated projectile entities.

Both models have `Birth - 1`, `Birth - 2`, `Birth - 3` (each 1600
model-time units) and `Death`, and include ribbon emitters. Their four
inspected shared textures remain resolved by the generic MDX renderer.

**Retail screenshot evidence (classic TFT level 3):** the Warden produces
many outward-travelling knives with extended green ribbons, including when
no enemy unit is nearby. The number of visual blades must not follow the
maximum damage target count or the actual list of damaged enemies. The supplied
screenshots show level 3 only; they do **not** establish exact counts for levels
1 or 2, nor whether numbered Birth animations are selected by rank.

`G_AbilityEffectArt` honours custom ability `CasterArt` and `MissileArt` and
inherits the base ability row. When a stock AEfk art field is absent, the
original models provide fallbacks (explicit `-`/`_` disables them). The caster
burst remains one ordinary transient at the cast-position snapshot.

Each visual knife is a temporary, server-authored **fixed-point presentation
projectile** with a unique outward bearing, a snapshot destination at the
ability's authored Area radius, and no gameplay damage or homing target.
`G_SpawnModelEffect` supplies temporary MDX lifecycle and ribbon rendering;
`MOVETYPE_FLYMISSILE`, `G_AddChannel` and `G_StartProjectilePresentation`
reuse existing game-side movement and network presentation (no new renderer,
wire fields, or per-target game missiles). Its normal endfunc frees it on
arrival. Visuals continue whether or not any enemies are present.

A **provisional** level-to-knife density (8/12/16 for levels 1/2/3) gives
visibly fuller higher-ranked casts. Emission is deterministic and radial;
small speed offsets spread the ribbons across the area instead of piling all
knives onto a single ring. The same numbered `Birth - N` is provisionally
selected according to rank, with the default animation fallback for custom
models. Exact counts, trajectory speed, birth-variant selection, and the MDX's
own keyframe displacements still need comparison with level 1/2/3 retail
captures; these settings are not claimed as recovered retail constants.

The actual Fan of Knives hit and enhanced-physical damage are still committed
immediately and independently in `CAbilityFanOfKnives`; reaching a visual
flight destination must never damage a unit, consume DataB or fire another
spell event. The authored `Effectsound` plays once per cast. The three
inspected `FanOfKnivesMissileHit{1,2,3}.wav` files are not automatically
played per target: no reliable selection/timing contract was established.

A map that intentionally sets the same CasterArt and MissileArt is shown once,
so custom caster-only art is not accidentally cloned into many moving objects.

## Verification

Synthetic `AbilityData` fixtures in
`games/warcraft-3/game/tests/t_fan_of_knives.c` exercise the actual no-target
cast with a stock-shaped 70/420 row and a custom 51/110 row. Cases cover more
candidates than the cap, allied exclusion, range, numeric armor, Hero defense,
Ethereal and spell-immune exclusion, `Bam2` survival, and a changed
`DamageBonusSpells` gameplay constant.

Run `make test-wc3-engine WC3_PATTERN='wc3_spell.fan_of_knives*'` after building.
Visual knife counts and travel, numbered Birth selection, MDX texture slot references,
audio event policy, and retail over-cap victim order still need an in-game comparison. The authored
model/animation lifecycle and custom-alias presentation have isolated tests.

See also [Attack Damage](attack-damage.md), [Anti-Magic Shell](anti-magic-shell.md)
and [Adding Warcraft III Abilities](ability-implementation.md).
