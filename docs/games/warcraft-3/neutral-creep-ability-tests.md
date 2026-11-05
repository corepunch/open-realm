# Neutral creep ability contracts — headless tests

The [coverage matrix](neutral-creep-ability-test-matrix.csv) inventories **all 300
named creep–ability rows** (261 distinct creeps overall, including 87 with no
named ability) transcribed from Blizzard's classic Battle.net creep index. It
keeps the source name, page URL, paraphrased effect, and an explicit automation
status, plus separately reported unit IDs, `abilList` and verified aliases where available. The source spreadsheet is a *secondary reference* and may omit retail
ability-specific targeting, duration or ROC/TFT differences.

## Executable gameplay coverage

`games/warcraft-3/game/tests/t_neutral_creep_contract.c` registers **299 independent
headless gameplay tests**, indexed by the 1-based source rows: **95 original synthetic
regressions** for twelve mechanics and **204 added gameplay tests** for
80 further ability labels. They deliberately assert production outcomes even where
the corresponding mechanics are missing; none is skipped or marked expected-failure.
The original group is:

| Mechanic | Creep–ability entries | Production checks |
|---|---:|---|
| Heal (Autocast) | 10 | Autocast heals the described amount, switching off prevents another cast, enemy invalid |
| Abolish Magic (Autocast) | 10 | Friendly debuff/enemy buff removal, friendly buff preserved, summoned damage, ordinary enemy unaffected |
| Ensnare | 10 | Enemy rooted, friendly target invalid, lock and release at described expiry |
| Rejuvenation | 6 | Allied healing-over-time rate, hostile target invalid, expiry stops healing |
| Evasion (Passive) | 7 | Production attack-hit path at deterministic 0% and 100% branches |
| Bash (Passive) | 8 | Attack damage bonus, 0%/100% branches, stun and expiry |
| Spell Immunity (Passive) | 18 | Magic damage blocked, physical hit allowed, skill removal restores spell damage |
| Slow (Autocast) | 8 | Enemy-only attack/movement modifiers and expiry |
| Curse | 6 | Enemy-only miss-chance status and expiry |
| Bloodlust / Bloodlust (Autocast) | 5 | Allied attack/movement boosts, expiry, enemy rejected |
| Faerie Fire | 3 | Enemy-only armor penalty and expiry (not visibility) |
| Roar | 3 | Nearby allied attack bonus and expiry |

Each case is registered as `wc3_creep_contract.row_N` and identified by
its source spreadsheet row and creep name in
`t_neutral_creep_contract_cases.inc`. `AbilityData` is injected via the same
production `G_SetSLKRows` hook as existing tests, and actions run through
`G_TryUnitAutocast`, `S_Cast*Spell`, or `S_ResolveAttackHit`.

The supplied archive audit report identifies the following rawcodes (among others): `ndtp` /
`nitp` use `Anh1` (12 HP), `ndth` uses `Anh2` (25 HP), `nfsh` / `nith`
use `ACd2` and `nfsp` / `nfps` use `ACdm` (300 summon damage). The
corresponding test cases use their real *ability alias* and the stated
`DataA`/`DataB` value. Furbolg `ACr2`, Creep `ACen` and Golem `ACmi` aliases are also used
where that ownership was checked. Their unit IDs are kept as evidence in the
matrix, **not used as claims that the synthetic test archive contains full
retail creep data**. Other Abolish fixtures use non-stock `DataB=137`, rather
than copying an unverified 75 damage amount (75 is the cost of verified
`ACdm` and `ACd2`).

The fixtures are synthetic by design: the test archive does **not** contain the retail neutral
unit/ability tables. **These tests do not independently prove that the named creep has that ability
rawcode, level or object-data cells. The candidate assignments come from the
user-supplied archive audit report, not an archived fixture within this repository.**
Chance tests exercise deterministic 0% and 100% branches, not the statistical
15% frequency. Qualitative source
claims without numbers use a deliberately non-stock fixture value; do not
interpret that value as a retail constant. The tests exercise
ability behavior but do not exercise every final production UI/order path.

## Added 204 gameplay tests

`t_neutral_creep_contract_extended_impl.inc` implements concise, shared fixture
and assertion recipes. `t_neutral_creep_contract_extended_cases.inc` records
**each of the 204 individual creep/ability pairs** with source row, creep name,
URL, description, representative parent code, and verified alias when known.
Each is registered under the existing `wc3_creep_contract.row_N` prefix; there
are no feature files or new test-only runtime functions.

The new fixtures inject one AbilityData row with **distinct non-retail numeric
values** (e.g. 100% critical/splash probability, explicit damage and duration,
area and a synthetic `hfoo` summon) through `G_SetSLKRows`. Assertions exercise
production cast paths (`S_CastUnitTargetSpell`, `S_CastPointTargetSpell`,
`S_CastNoTargetSpell`), real status/attack, aura, resurrection, corpse and
summoning paths, with observable effects. Shared negative controls include
out-of-range hostiles, friend/foe effects, expiry, and secondary victims.
**A successful cast or registered ability alone does not pass an extended test.**

The focused `make test-wc3-engine WC3_PATTERN='wc3_creep_contract.*'` run passes
all 299 cases (1,727 assertions) in both classic and TFT modes. That verifies
the current synthetic gameplay contracts;
it does not independently prove every candidate ability assignment or field
against retail object data. Generic synthetic fixtures are not a substitute for
the actual `Data*`/`BuffID` fields, channel timing, missiles, attack profile,
corpse requirements or cooldown. **Before adding a retail-parity claim, validate
the rawcode, BuffID, UnitID, targeting and numeric fields against both archive
schemas.**
The Makrura Summon Prawns fixture deliberately exercises the shared `AOsf`
summoning path, **not** `ACsp`: the latter is registered as the unrelated
*Natural creep sleep* passive. Its actual retail alias remains unverified;
object-data binding must replace this stand-in before it can establish Prawn
ownership or timed life. A few shape-specific effects (e.g. Lightning Attack's
attack type, summons from trees, Devour ingestion) still need *additional*
retail-bound assertions; the
new tests cover a positive gameplay outcome, not complete retail parity.

| Source rows | Status | Meaning |
|---:|---|---|
| 299 | Synthetic gameplay contract | Registered and passing in classic and TFT engine modes |
| 2 (rows 7, 198) | Contradicted by verified `abilList` | No executable case for the disputed source assignment |

## Retail object-data priority

Use local ROC `Units\UnitData.slk`/`Units\UnitAbilities.slk` and the TFT
overlay as the authority for unit-to-ability assignments. Resolve every
`abilList` alias to `code=` and its parent, then read the alias's actual level,
`Data*`, `targs`, `BuffID`, `UnitID`, `Dur`, `Area`, `Cost` and `Cool` fields.
The **300-row Battle.net-derived spreadsheet is a proposed coverage inventory**,
not authoritative object data. The verified audit establishes `ndtp`/`nitp`
own `Anh1` (12 HP), `ndth` owns `Anh2` (25 HP), and checked `ACdm`/`ACd2`
have `DataB=300` summon damage while 75 is mana **cost**. Rows 7 and 198
wrongly assign Abolish to `ndtp`/`nitp` according to the verified `abilList`.
Rows labelled `Forest Troll Priest` must not silently inherit the rawcode
`nfsp` (verified as *Forest Troll Shadow Priest*) until identity is resolved.
`ntrd` Dragon Turtle and `nhyc` Campaign Turtle are distinct units.

## Remaining retail-binding gaps

- The synthetic cases prove the behavior exercised by their fixtures, not every
  named creep's retail `abilList`. Keep the source-row mapping marked as
  unverified unless it is backed by the archive audit.
- Rows 7 and 198 remain excluded because the verified `abilList` contradicts
  the reference spreadsheet's assignment.
- The Makrura Summon Prawns fixture exercises the shared `AOsf` summon path, not
  `ACsp`; its actual retail alias remains unverified.
- Some shape-specific cases still need retail-bound assertions, including
  Lightning Attack attack type, summons from trees and Devour ingestion.

Run the classic and TFT variants independently with:

```sh
make test-wc3-engine WC3_PATTERN='wc3_creep_contract.*'
```

Follow [ability implementation](ability-implementation.md),
[creep aliases](creep-ability-aliases.md), and the root `CONTRIBUTING.md` for
ROC/TFT `Data*` columns, status lifecycle, production paths and fixture rules.

The tests cover source-specific fixes including creep Reincarnation, alias-aware
Critical Strike, Frost Armor's DataB armor value, delayed projectile impacts,
periodic status damage, permanent Immolation and Parasite minion creation.
The fixture values establish handler behavior; archive-derived object tables
remain authoritative for exact retail assignments and values.
