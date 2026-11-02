# Mountain Giant War Club (`Agra`)

War Club is a tree-targeted, no-mana active spell, not a passive damage modifier.
Blizzard's [Mountain Giant guide](https://classic.battle.net/war3/nightelf/units/mountaingiant.shtml)
describes 15 club strikes, a switch from Normal to Siege damage, improved
attack range, and Smart/right-clicking trees. The equipped form selects the
unit's authored **Attack 2**; Attack 1 remains intact.

## Server ownership and paths

- `skills/s_skills.c` registers `Agra` with `AB_SPELL`, an entity-aware point
  target cursor, and the `grabtree` order.
- `skills/s_creep_abilities.c` validates a live, visible, non-invulnerable
  `TARG_TREE`, uses `G_KillDestructable` for the tree-death/pathing/event
  lifecycle, and maintains two unit-state fields: `war_club_ability` and
  `war_club_attacks_remaining` (zero means unequipped).
- `skills/s_spell.c` retains the target *entity* through the spell's Move
  approach, including its `spawn_time` via `channel_t`. This matters for trees:
  freezing a position or choosing the nearest tree at completion can consume
  a different tree, or one occupying a recycled edict slot.
- The Mountain Giant may cast at tree-footprint range: nominal `Rng` plus the
  caster and target collision radii. The caster still uses the ordinary Move
  pathing system. If no route exists, the target is not consumed.
- `m_unit.c` already dispatches Smart target orders to authored abilities
  before harvesting/destructable attack. The `Agra` procedure consumes valid
  Smart-on-tree orders; explicit `grabtree` orders go through the common
  `S_IssueUnitTargetSpell` branch for entity-targeted point abilities.
- `skills/s_attack.c` forces enabled attack mask 2 while the club is equipped,
  otherwise retains the unit's normal mask. All combat consumers continue to
  select the effective authored attack profile, including target filtering,
  range, cooldown, dice and damage/armor interaction. At the weapon damage
  point, a committed swing consumes one charge *after* resolving that swing.
- `hud/hud_infopanel.c` exposes the Attack 2 damage profile in the equipped
  form. The game module, not the generic client, owns this presentation.
- `g_save.c` serializes both state fields; save envelope version 123 rejects
  older layouts. No entity pointer is stored for the consumed tree.

## Animation

War Club uses the Mountain Giant model's `Upgrade` animation tag, according
to community asset investigations ([Hive Workshop animation tags](https://www.hiveworkshop.com/threads/valid-animation-tags.292980/)
and [Mountain Giant animation example](https://www.hiveworkshop.com/threads/mountain-giant.174949/)).
`G_AddUnitAnimationProperties(..., "upgrade", ...)` enables/disables the tag.

**Remaining art parity:** The tree weapon's texture appears to be a model
replaceable texture indexed from the harvested tree. The current game/client
presentation interface does not expose this per-weapon texture mapping. Test
classic stock MDX and art before considering the visual component fully
retail-equivalent; do not bake a specific tree texture into the generic client.

## Intentional limits / retail checks

- The default attack limit is **15 swings**. These are committed at damage
  point (including a miss), not attack orders or completed movement. Exact
  retail evasion/miss counting requires capture.
- Re-grabbing while already equipped is rejected to avoid duplicate tree
  consumption. Confirm retail recast policy separately.
- The basic equipped state does not consume mana or time-expire; normal spell
  cooldown/mana fields remain data-driven through the common cast pipeline.
- Death and ability disable/remove clear equipped state and animation tag.
  More unusual transformation/revival combinations need retail checks.
- Source tests cover tree eligibility/death, switching mask, 15 strikes,
  double-grab protection, and state persistence; visual correctness requires
  testing with the retail assets in the running game.

## Suggested manual validation

1. Load a TFT map with Mountain Giants and trees. Click War Club on a nearby
   tree, then right-click another one with an unequipped Giant; verify both
   take trees and use the `Upgrade` animation.
2. Move a Giant to a distant tree. Destroy the tree while it approaches; no
   other tree should be grabbed and no club should appear.
3. Attack a building and ordinary ground units. The attack icon/type should
   show Siege, and the normal Attack 1 profile should resume after swing 15.
4. Save with 7 strikes remaining, reload, and confirm only those 7 remain.
5. Compare club texture/animation, missed-attack charge accounting and recast
   policy against retail Warcraft III.
