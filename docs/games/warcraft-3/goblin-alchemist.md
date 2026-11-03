# Goblin Alchemist abilities (TFT)

The Goblin Alchemist uses four neutral hero abilities. They are implemented
in the Warcraft III game module; the generic client only plays the models,
animations and sounds requested by the game.

## Healing Spray (`ANhs`)

The existing `S_SpellChannelThinker` fires `DataF` waves, once at cast and then
at `DataB`-second intervals. Every wave heals eligible units for `DataA`,
scaling the heal when `DataA * target_count > DataD`. Targets are **friendly
organic** ground/air units (including the caster) in `Area`, per active TFT
`targs`. The older web claim about enemy healing contradicts the source data.

`DataC` now spawns visual `HealBottleMissile` projectiles on every authored
wave, with `Missilespeed` from `NeutralAbilityFunc.txt` (stock 700). Their
fixed-ground destinations use a stable, dispersed distribution over the area.
They are presentation only: healing is still committed at the pulse, not on
bottle impact. The exact retail random spread and visual impact timing remain
unverified. Channel cancellation stops **new** waves, not bottles already
launched. Bottle models and their fixed destinations survive save/load through
the shared `umove_t`/channel serialization.

## Chemical Rage (`ANcr`)

`ANcr` uses the existing duration-based Metamorphosis transform into the
rank-specific `Nalm`, `Nal2` or `Nal3` as authored in `UnitID` (source `Nalc`).
The target forms themselves encode 405 movement speed and weapon cooldowns
2.0 / 1.42 / 1.11, compared with normal speed 290 and cooldown 2.5.
**Do not additionally apply `DataE` or `DataF` on top of those forms:** doing
so would double the authored movement/attack-rate benefit. `HeroDur=15`
controls form life, while `Dur=0.35` does not set the 15-second lifetime.
The forward and reverse changes now run the `Morph` / `Morph Alternate`
sequences and select alternate animation properties while enraged. The
existing `morph_end` callback and saved transform thinker own restoration.
Precise switch-frame timing, death/revival interactions and sound events
still need comparison against retail.

## Acid Bomb (`ANab`)

`ANab` launches a homing `BottleMissile` at `Missilespeed` (stock 700).
The primary target is revalidated at impact, including Spell Shield. On a
successful impact, all eligible enemies in `Area` (stock 200), including
original target, receive `BNab` as a timed negative magical status.

* `DataC`: armor reduction as a **negative status modifier**, not a permanent
  change to the unit's base armor.
* `DataD`: primary target damage per second (8 / 13 / 18).
* `DataE`: secondary splash damage per second (5 / 8.5 / 12).
* `Dur` and `HeroDur`: 15-second status duration in the stock data.
* The authored buff TargetArt (`BottleImpact`) is attached to the chest.

Each affected unit gets a damage thinker bound to its buff **instance ID**,
owner incarnation and target incarnation. One-second pulses preserve fractional
DPS. A replaced/dispelled buff prevents an older thinker from applying more
damage or removing the newer debuff. Death and ordinary expiry also end the
thinker. Shared status serialization saves the debuff, armor modifier and its
identity; `acid_bomb_think` is already on `g_save.c`'s callback roster. The
initial per-target status application samples victims only **at impact**;
units entering the area later are not retroactively affected.

With `BZ_COMPAT_WORKAROUNDS`, missing 1.07 `BuffID` on stock `ANab` falls back
to `BNab`; an authored usable BuffID still takes precedence.

Unconfirmed retail details: whether DOT starts immediately at impact versus
one second later, the exact refresh policy for simultaneous casts and any
special treatment of flying/immune units beyond the authored target mask.

## Transmute (`ANtm`)

`DataA * goldCost` and `DataB * lumberCost` are credited through the existing
resource-income policy. Where `GoldBottleMissile` art exists, the kill,
conversion credit and `BNtm` gold-pile special art happen at **projectile
impact**, not launch. The target is revalidated and can be intercepted by
Spell Shield; an invalid or recycled target receives no conversion. A custom
ability without missile art retains the instantaneous fallback.

`DataC` is the level cap; Heroes and resistant targets are invalid.
`DataD > 0` permits the target's ordinary death bounty. `DataD <= 0`
suppresses only that bounty via `AI_TRANSMUTE_NO_BOUNTY` during `unit_die()`;
regular hero XP, death events, corpse handling and conversion income still
follow the native death path. The temporary policy bit is cleared before the
cast completes and does not add a new save field. Note that any effects of
upkeep on the conversion payment depend on `G_CreditResourceIncome`.

## Verification

No local build or test execution was performed while authoring this patch.
The existing Healing Spray/Transmute tests remain in place. Focused synthetic
fixtures check `DataC` bottle count, Acid Bomb splash/armor/DOT/dispel and
DOT thinker save/load, and `DataD=0` Transmute bounty suppression.

Recommended in-game comparisons: Acid Bomb one-target vs splash DOT tick
cadence; simultaneous recasts and dispels; Chemical Rage level-specific movement,
attack and morph clip lengths; GoldBottle travel/impact and kill events; and
Healing Spray visual bottles during early channel interruption.
