# Warcraft III Night Elf Moon Glaive (`Remg`)

## Contract

Huntresses already have a two-target **Missile (Bounce)** (`mbounce`) attack:
its initial hit may bounce to one other enemy **before** the research. Completing
Moon Glaive (`Remg`) at the Ancient of War adds one more target, giving three
consecutive targets (initial plus two bounces). Each successive hit deals half
of the preceding hit's damage with standard authored `udl1=0.5`. No target can
be struck twice in one chain. The original Warcraft III Huntress page documents
this 20 → 10 → 5 damage progression and the extra researched bounce:
[Blizzard's Huntress reference](https://classic.battle.net/war3/nightelf/units/huntress.shtml).

This is **not** an ability unlocked from zero bounces. `Amgl` is the normal
passive, while `Amgr` is the independent/no-research passive used by special
units. Research is player-owned and must not grant its effect to a Huntress of
another owner. Custom unit types participate only if `UnitBalance.upgrades`
(`Upgrades Used`) lists `Remg`; they may author the base target count and
loss factor on their own weapon profile.

## Code and data flow

- `s_attack.c` treats `WPN_MBOUNCE` as a ranged attack; its initial glaive
  travels via the regular `fire_rocket` physical-missile path. `g_monster.c`
  initializes the projectile art/trajectory, and `g_save.c` re-registers the
  art after loading. This avoids the pre-existing bug where `mbounce` weapons
  took the melee hit path and never displayed their initial missile.
- `S_ResolveAttackHit` calls `S_MoonGlaiveAttack` only after ordinary primary
  evasion/miss/damage checks. `Amgl`/`Amgr` ability ownership is mandatory.
- `UnitWeapons` `utc1` (runtime `attack1.maxTargets`) determines the base
  *total*, including the primary target. A zero value falls back to
  `AbilityData.DataA` if positive, then stock total 2. This is bounded to 8
  targets to avoid runaway custom configurations.
- `G_MoonGlaiveResearchLevel` resolves the current owner and the unit's
  `Remg` participation. A positive level adds **one** target to `Amgl`, never
  to `Amgr`. It consults live tech state, so normal completion, JASS
  `SetPlayerTechResearched`/`AddPlayerTechResearched`, revocation, ownership
  changes and save/load work without storing extra per-unit state.
- `UnitWeapons` `udl1` (runtime `attack1.damageLoss`) controls fractional
  damage loss per bounce. Missing/zero data currently uses the standard
  50% fallback. Ability `Area` overrides the stock 400-unit bounce radius
  when positive. Bounce candidates must be alive, enemies, visible for
  targeting, not wards, compatible with the primary weapon's target mask,
  and within range of the most recently hit unit. The chain selects the
  nearest eligible unvisited target.
- Secondary hits use `T_Damage` with already-mitigated primary damage and
  repeated loss; they do not invoke primary-only on-hit/attack effects or
  apply armor twice.

## Current limits

The primary glaive now has projectile travel and presentation, but secondary
bounces currently resolve immediately at primary impact. **A travelling
projectile between successive targets and per-secondary-target defense/armor
recomputation remain future parity work.** The secondary damage is derived
from the final mitigated primary hit (without a second mitigation pass),
consistent with the existing OpenRealm secondary-effect contract. The modern
(2025+) rule changing Huntress armor to Heavy on research is likewise not
implemented here; it requires explicit modern-balance support, and must not
silently alter ROC/TFT behavior.

These limitations should not be confused with the implemented research-owned
extra bounce and damage-loss rules. Do not replace this with an `Amgl` rank
requirement that disables the Huntress's pre-research bounce.

## Validation

- `wc3_spell.moon_glaive_bounces_attack_to_nearby_enemy`
- `wc3_spell.moon_glaive_stock_zeros_bounce_inside_attack_range`
- `wc3_spell.moon_glaive_research_grants_and_revokes_second_bounce`
- `wc3_spell.moon_glaive_custom_targets_and_no_research_variant`

For manual verification, use a Night Elf Huntress attacking three enemy ground
units within 400 of each other. Before research only two should take damage;
after completing `Remg` three should take decreasing damage. Cancel/revoke the
research and verify it returns to two. Verify the initial glaive visually
launches rather than hitting as a melee weapon. The current tests exercise
functional damage/research; a visual flight test is still needed for chained
projectile presentation.

See also [Attack Damage](attack-damage.md) and
[Research and Upgrades](research-and-upgrades.md).
