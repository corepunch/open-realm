# Retail eight-item restoration (installed TFT archive report, 9 November 2026)

## Evidence and scope

This patch uses the supplied `War3x.mpq` extraction and `NightElfX02.w3x`
`war3map.j` report. The installed Warcraft III patch revision was not
established; these are **dataset-specific expectations**, not universal
cross-version balance constants. No Warcraft retail archive is required by
production code or tests. All numeric behaviour is read from the loaded
ItemData and AbilityData rows.

| Item | Concrete ability | Authored behaviour in extracted TFT data |
|---|---|---|
| Rune of Rebirth `rreb` | `AIrb` | One-use pickup; existing rebirth handler retained |
| Rune of Spirit Link `rspl` | `Aspp` | 50% damage sharing, DataB=50, radius/range 1400, 75 seconds, `Bspl` |
| Lion Horn `lhst` | `AIad` | Devotion Aura DataA=1 armor, area 900, `BHad` |
| Shield of Deathlord `shdt` | `AIlf,AImz,AId0,AIcf` | +150 max HP, +100 max mana, +10 armor, item Immolation |
| Cloak of Flames `clfm` | `AIcf` | DataA=10 DPS, area=160, `ground,enemy,neutral`, `BIcf` |
| Potion Invulnerability `pnvu` | `AIvu` | `Bvul`, 15 seconds, Cool=45, one use |
| Skeletal Artifact `skrt` | none | Quest-carried item; no intrinsic active spell |
| Wand of Neutralization `wneu` | `AIdc` | 4 charges, range 700, chain area 400, DataC=8, DataA/B=0 |

## Implementation contracts

- Instant inventory and HUD uses both reach `G_UseItemWithController`, with
  caller identity preserved for shared control. Cooldown begins only on
  successful `AB_ITEM` use; charge/event handling remains in
  `G_CompleteItemUse`. Unit-target item spells retain the shared cast workflow.
- `Bvul` contributes timed `WC3_STATUS_STATE_INVULNERABLE` and does not
  overwrite the independent scripted `UNIT_FLAG_INVULNERABLE`. Spell target
  masks opt into invulnerable recipients with `invu`. Damage, the HUD and
  JASS direct damage see effective rather than flag-only invulnerability.
- `AIcf` uses the status-owned periodic clock and its **own** `BIcf` state,
  separate from toggle/permanent Immolation's `BEim`. Pickup/drop/transfer
  trigger source reevaluation. Two held `AIcf` providers currently contribute
  one family pulse, pending retail stacking research. `TARG_MECHANICAL` and
  `TARG_ORGANIC` are material classifications in this engine, not proof that
  the unit is airborne; a ground target mask includes both.
- `AIad` is registered as a Devotion Aura provider by **ability class**, not
  `G_AbilityCode(AIad)==AHad`, because the extracted alias reports
  `code=aura`. Owned-item discovery invalidates aura caches on inventory
  mutation and reads the concrete item alias's rank-one values.
- `AIdc` and `Ache` are unit-target chain spells; `DataC` limits the number
  of recipients, `Area` controls hop radius, and `DataA`/`DataB` govern
  optional mana loss/summon damage. The implementation chooses nearest
  eligible unvisited units deterministically. Exact retail chain selection
  and whether the maximum counts the initial target need runtime comparison.
- Spirit Link damage redistribution can exceed the original 64-recipient
  intermediate buffer when independent groups coexist. The patch scans up to
  `MAX_ENTITIES` while preserving original share and rounding logic.

## Campaign handling

The external `NightElfX02.w3x` script creates a **single** `skrt` item after
three Summoning Pit deaths, keeps its actual item handle in
`udg_SkeletalArtifact`, checks that the returning unit carries this same
handle in `gg_rct_Hermit_Tripwire_A`, removes the item on cleanup and grants
`ciri` (Robe of the Magi) to Maiev. This is script-driven: **do not** add
an artificial `skrt` active ability or invent a quest completion native.
The map script is not present in this source archive; a full integration
regression requires running the external installed mission.

## Coverage and remaining verification

New synthetic test cases under
`games/warcraft-3/game/tests/t_retail_item_effect_regression.c` exercise:
nonstock item duration/cooldown and invulnerability-source overlap, nonstock
Immolation DPS and mechanical targets, the Shield's four item abilities,
nonstock Lion Horn armor and pickup/drop, and bounded chain dispel. Existing
rune tests remain unchanged.

**Retail-runtime follow-ups:** two Cloaks versus Cloak+Shield versus native
Immolation stacking; first damage tick and damage-type interactions; `AIdc`
chain hop ordering and count interpretation; spell/dispel exceptions under
`Bvul`; Rune of Rebirth's underlying creep selection; ROC/TFT differences.
Do not treat these unverified observations as proof of retail parity.

**Authoring policy:** Static diff validation only. The patch author did not
compile or run tests; the user runs the WC3 test suite and game missions.
