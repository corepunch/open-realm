# Creep Ability Aliases

Creep `ACxx` / extra AbilityData rows are `code=` aliases of an already-registered
parent procedure. Register them as extra `abilitylist[]` rows on that parent in
`s_skills.c`. Do not create `s_creep.c` for the dump: procedures stay in the
owning `s_*.c`. The only creep-only procedure file is `s_creep_sleep.c`
(natural camp sleep).

## Mapping

Poll ROC then TFT with `ability_audit -raw` and copy `code=`. `FindAbilityForCommand`
already resolves a fourcc through `G_AbilityCode`, so the alias row is for
`FindAbilityByClassname` and for coverage. Shared hooks that key on the hero
rawcode (`G_UnitAbilityLevel(ANms)`, `G_UnitStatusLevel(AHfa)`) must resolve the
owner's alias the same way regen auras do for `ACnr`→`Aoar`.

| Alias | Parent `code=` | Procedure |
| --- | --- | --- |
| `ACcb` | `AHtb` | `CAbilityThunderBolt` |
| `ACde` / `Adsm` / `Adcn` | `Advm` / `Adis` | `CAbilityDispelMagic` |
| `ACca` / `ACcv` / `ACc2` / `ACc3` | `AUcs` | `CAbilityCarrionSwarm` |
| `Aenr` / `Aenw` | `AEer` | `CAbilityEntanglingRoots` |
| `ACmf` | `ANms` | `CAbilityManaShield` (orders + DataA/DataB via alias) |
| `ACsa` | `AHfa` | `CAbilityFlamingArrows` (toggle status is `ACsa`) |
| `ACpy` | `Aply` | `CAbilityPolymorph` (ROC empty BuffID → `Bply` in `human_buff`) |
| `ACsl` | `AUsl` | `CAbilitySleep` (ROC empty BuffID → `BUsl`) |
| `Ane2` | `Aneu` | shop select (`CAbilityPassive`), not Inventory |

Do not register as the parent until that procedure exists:

| Alias | `code=` | Class | Why it stays TODO |
| --- | --- | --- | --- |
| `ACmo` | `ANmo` | `CAbilityMonsoon` | not Forked Lightning |
| `ACf3` / `ACfd` / `Afod` | `ANfd` | `CAbilityFingerOfDeath` | not Firebolt |
| `ACwb` | `Aweb` | `CAbilityWeb` | active air-only, not `AB_PASSIVE` |
| `AHta` | `AIta` | `CAbilityItemDetectAoe` | active Reveal, not passive |
| `Ache` | `AIdc` | `CAbilityItemDispelChain` | active chain dispel, not passive |

ROC empty BuffID fallbacks belong in the parent procedure's buff helper, using
the TFT token. Do not invent fourccs (`Bslp`) or put Polymorph/`ACsa` entries in
`melee_buff_fallback`.

## Verification

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw ACmo
make test-wc3-engine WC3_PATTERN='wc3_spell.creep_*'
```
