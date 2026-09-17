# Adding Warcraft III Abilities

This project implements Warcraft III abilities from the authoritative game data and
observable gameplay contract. The goal is useful, testable compatibility. TFT's extracted
class hierarchy supplies behavioral evidence; the [flat C ability plan](ability-inheritance-plan.md)
uses Quake 2-style flags, enums, shared processors and message procedures without runtime object inheritance.

`AbilityStrings.txt` is the starting point because it states what the player is
supposed to observe. It is not a complete specification, so every implementation
record separates confirmed facts, reasonable deductions, and unresolved behavior.

Ability-owned behavior is the default architecture: keep the full behavior in its ability module and reach it through
generic dispatch. Start with [Mechanical Workflow](#mechanical-workflow); see [ownership](#ability-owned-orders-and-persistent-behavior)
and [testing](#testing) before adding code.

## Mechanical Workflow

Do these steps in order. Do not skip the poll. Do not write a Python SLK parser.
Do not open War3.mpq by hand unless the brief is missing a field you can name.

### 1. Poll ROC and TFT in one command

```sh
make build/bin/ability_audit
build/bin/ability_audit -data 'data/Warcraft III' -raw Aams
```

`-raw` without `-roc`/`-tft` prints a ROC section then a TFT section. That split is
mandatory: TFT `War3x.mpq` overlays `AbilityData.slk`, and the same rawcode can
change cost, `targs`, `BuffID`, and even which aliases exist.

The brief already contains:

| Block | What to copy into the implementation record |
| --- | --- |
| `id` / `code` / `comments` | registry rawcode vs shared procedure identity |
| `class` / `parent` | `CAbility*` name and TFT parent (`AAsm`, `AAat`, `AAcs`, …) |
| `L1` `targs` `cost` `cool` `rng` `dur` `heroDur` `area` | targeting and `spell_cmd` inputs |
| `DataA-I` plus any non-zero `DataC=` / `DataB=` line | authored numbers via `S_SpellData`; unitCode/abilCode cells (`ncgb`, `ANin`) print as fourcc — read those with `S_SpellDataId` |
| `BuffID` | comma list; each token is a different buff |
| `strings` `Name`/`Ubertip`/`Untip` | visible contract and inverse/autocast |
| `buffs` `Buffubertip` | what the status actually does (immunity vs shield, etc.) |
| `aliases of code=` | other rawcodes that share this `code` and should share the procedure |

Restrict to one archive set only when comparing a single overlay:

```sh
build/bin/ability_audit -data 'data/Warcraft III' -roc -raw Aams
build/bin/ability_audit -data 'data/Warcraft III' -tft -raw Aams
```

`-roc` opens War3.mpq only. `-tft` searches War3x.mpq first, then War3.mpq.
If a later agent uses `mpqtool cat Units/AbilityData.slk` after `-data` opened
every archive, they will silently read the TFT overlay and think ROC matches.

### 2. Turn the brief into a one-page contract

Write (or update) `docs/games/warcraft-3/<ability>.md` from the brief before
coding. Record:

- which rawcodes share `CAbility*` (`code=` plus the alias list);
- which `Data*` fields the execute path will read;
- which BuffID token applies when (empty DataC vs authored DataC is a real split);
- ROC-only omissions (missing `BuffID` is allowed; do not invent DataC);
- TFT-only rows (`AbilityData: not found` under ROC).

Look up the parent in the [shared behavior table](#2-identify-shared-behavior).
`AAsm` is the simple-spell pipeline, not “this is easy”.

### 3. Register every concrete alias, share one procedure

Add one `abilitylist` row per `AbilityData.alias` that this change implements.
Point them at the same `CAbility*` procedure. Read data through
`abilityitem_t.code` so `Aams` and `Aam2` keep their own `targs`/`DataC`/`BuffID`.
Do not register abstract TFT classes (`AAsm`, `AAat`). Skip `Aami` / item rows
until their own brief is in scope.

### 4. Tests first, from the brief

Use the production cast path (`S_CastUnitTargetSpell` / `S_CastNoTargetSpell`).
Put the test in `games/warcraft-3/game/tests/t_*.c` with suite `wc3_spell`.
The `TEST(suite, name)` identifier is `suite.name`, so the glob has no extra dot:

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.anti_magic_shell*'
```

`WC3_PATTERN='wc3_spell.anti_magic_shell.*'` matches zero tests.

Minimum coverage, taken from the brief:

- procedure lookup for each registered alias;
- authored field mapping (use a non-stock DataA/DataC in the fixture so the
  test cannot pass on a hardcoded retail constant);
- positive path and the nearest invalid target (`targs`, dead, wrong owner);
- duration/expiry or pool-break, and recast;
- save/load when new persistent state is added (`abilstatus.data`, edict fields).

Enemy targeting tests must mark both players `kPlayerTypeHuman` and clear
`level.alliances`. `setup_test_world()` leaves `kPlayerTypeNone`, which makes
`S_SpellIsEnemy` false even when player ids differ.

### 5. Implement in the owning `s_*.c`

Keep execute/validate/absorb in the ability file. Hook shared predicates
(`S_UnitSpellImmune`, `S_SpellDamage`) only when the brief’s buff contract
belongs on that path. Prefer branching on authored data over a second
procedure.

### 6. Verify the focused pattern, then the family

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.<name>*'
make test-wc3-engine WC3_PATTERN='wc3_save.*'   # only if edict_t / abilstatus changed
```

Do not debug `make test` `+test '*'` while finishing one ability. Never
`globals.RunFrame()` or `level.started`/`scriptsStarted` in spell tests;
that path dies in `jass_runevents(NULL)`. Advance buffs with
`unit_updatestatuses` and thinkers with `G_RunEntities`.

### 7. Batch: one agent per unrelated group

When finishing several abilities in one pass, split the list into groups that
do not share a procedure, `s_*.c` owner, or shared hook (`S_SpellDamage`,
`S_UnitSpellImmune`, `edict_t` fields, `abilstatus` payload). Spawn one
subagent per unrelated group. Each child polls its own rawcodes, writes its
own tests and docs, and commits only its group.

Keep in the same agent (do not split):

- `code=` aliases of one implementation (`Aams` / `Aam2` / `ACam`);
- rawcodes that already share a procedure or will after the change;
- anything that edits the same shared predicate or `edict_t` layout.

The parent agent owns `s_skills.c` registry rows if two groups would both
touch it; otherwise each child adds its own rows. Do not spawn a subagent per
rawcode when the brief shows they are the same `code=`.

## Sources Of Truth

Use sources in this order, choosing the smallest set that answers the question:

1. Active ROC/TFT archive data.
2. Existing OpenWarcraft3 runtime contracts and neighboring implementations.
3. Controlled observation in the retail game.
4. Independent references such as Liquipedia, Warcraft III wikis, and Warsmash.
5. `Game.dll` disassembly for details that remain ambiguous or cannot be observed.

Retail data is authoritative for authored values. External references are useful for
explaining behavior, but they must not override an active ROC/TFT row without a reason.
Game.dll is evidence about one binary build, not a portable source file.

## Data Flow

The normal ability path is:

```text
MPQ AbilityStrings.txt
  -> name, tooltip, hotkey, visible behavior
MPQ AbilityData.slk
  -> area, range, duration, cost, cooldown, targets, DataA-I, rawcode IDs
DDX/SLK loaders
  -> AbilityData_t and abilityLevel_t
ability registry
  -> ability_t
runtime consumers
  -> damage, status, movement, combat, summons, morphs, or presentation
```

Resolve values through the existing accessors rather than copying level tables into C:

- `S_SpellData(code, level, index)` for `DataA` through `DataI`.
- `S_SpellDataId(code, level, index)` for rawcode-valued data fields.
- `S_SpellNumber(code, field, level)` for `Area`, `Rng`, and related numbers.
- `S_SpellDuration(code, level, hero)` for `Dur` and `HeroDur`.
- `G_AbilityLevel(FS_SLKKey(classname), level)` when the complete normalized row is needed.

Levels and data indices are 1-based at the public helper boundary. Use `ability_audit`
to inspect ROC and TFT in one shot (see [Mechanical Workflow](#mechanical-workflow)):

```sh
make build/bin/ability_audit
build/bin/ability_audit -data 'data/Warcraft III' -raw AOws
```

## AbilityData.slk Column Reference

`AbilityData.slk` is the authoritative source for authored numeric and rawcode
ability data. It maps to `AbilityData_t` and its per-level `abilityLevel_t`
records in `g_unitrow.h`. The full game SLK has roughly 200 columns per level;
only the fields the runtime actually reads are present in `abilityLevel_t`.

### Per-ability columns (AbilityData_t)

| Column | Field | Notes |
| --- | --- | --- |
| `alias` | `id` | the rawcode looked up at runtime |
| `code` | `code` | the base-class rawcode (aliases share this) |
| `levels` | `levels` | number of levels authored for this entry |
| `reqLevel` | `reqLevel` | hero level required to learn |
| `levelSkip` | `levelSkip` | levels between learnable ranks |
| `hero` | `hero` | true for hero-only abilities |
| `item` | `item` | true for item-class abilities |
| `race` | `race` | faction string (`human`, `orc`, etc.) |
| `sort` | `sort` | ordering hint; not a gameplay value |

### Per-level columns (abilityLevel_t, suffix `N` = level index 1–4)

| Column | Field | Accessor |
| --- | --- | --- |
| `targs` | `targs` | target filter string; drives `S_SpellAllowsTarget` |
| `Rng` | `range` | `S_SpellNumber(code, ABILITY_NUMBER_RANGE, level)` |
| `Dur` | `dur` | `S_SpellDuration(code, level, false)` |
| `HeroDur` | `heroDur` | `S_SpellDuration(code, level, true)` |
| `Cool` | `cool` | `S_SpellNumber(code, ABILITY_NUMBER_COOLDOWN, level)` |
| `Cost` | `cost` (mana) | `S_SpellNumber(code, ABILITY_NUMBER_COST, level)` |
| `Area` | `area` | `S_SpellNumber(code, ABILITY_NUMBER_AREA, level)` |
| `Cast` | `cast` | cast-point delay |
| `DataA`–`DataI` | `data[0]`–`data[8]` | `S_SpellData(code, level, 1–9)` for floats; `S_SpellDataId` for rawcode IDs |
| `UnitID` | `unitID` | summoned-unit rawcode; `S_SpellUnitId(code, level)` |
| `BuffID` | `buffID` | buff rawcode applied on success; read via `G_AbilityLevel(code, level)->buffID` |
| `EfctID` | `efctID` | effect rawcode; drives `G_SpawnAbilityEffect*` with `WC3_EFFECT_EFFECT` |

`Data11`/`Data12`/`Data13` are the ROC column names for `DataA`/`DataB`/`DataC`
respectively. Both names refer to the same three fields; `S_SpellData` handles
both archive variants.

Pocket Factory (`ANsy`) is a data-driven timed producer: `UnitID` selects the
factory for the learned rank, `Dur` controls its lifetime, `DataA` is the
Clockwerk production interval, `DataB` is the rank-specific Clockwerk rawcode,
and `DataC` is each Clockwerk's timed life. A classless ability-owned thinker is
owned by the factory and runs through `G_RunEntities`; factory removal therefore
cancels production without adding Pocket Factory rules to general entity code.

### What is NOT in AbilityData.slk

**Projectile and missile art are not authored in AbilityData.slk.** They live in
two other places:

- **Unit weapon art** (`UnitWeapons.slk` / `UnitWeapons_t.attack[i].art`) — the
  missile model for normal and ranged attacks. This is per-weapon, not per-ability.
- **Buff/ability presentation** (`AbilityBuffData.slk` / the runtime buff row) —
  the `missileArt`, `targetArt`, `specialArt`, `effectArt` strings that drive
  `G_SpawnAbilityEffect*`. These are looked up by the buff rawcode (`BuffID`),
  not by the ability rawcode.

Other fields absent from AbilityData.slk: cast/target animation names, sound
sets, and the `WC3_EFFECT_CASTER`/`WC3_EFFECT_SPECIAL` art paths. Those come
from the buff row or from `AbilityStrings.txt` fields such as `animProps`.

### Reading a row in practice

```sh
# ROC section then TFT section, including strings, buffs, and code= aliases:
build/bin/ability_audit -data 'data/Warcraft III' -raw Ablo
```

In C, read a specific level's field through the public helpers:

```c
/* DataA of Bloodlust at level 1 (1-indexed): */
FLOAT attack_bonus = S_SpellData(MAKEFOURCC('A','b','l','o'), 1, 1);

/* Buff rawcode applied by level 1: */
LPCSTR buff = G_AbilityLevel(MAKEFOURCC('A','b','l','o'), 1)->buffID;

/* Summoned unit rawcode for a summon ability: */
DWORD unit_id = S_SpellUnitId(MAKEFOURCC('A','H','w','e'), 1);
```

Never hardcode per-level constants copied from the SLK; always read them at
runtime through the normalized row so the implementation works for both ROC and
TFT data sets and for any custom-map overrides.

## Reading AbilityStrings

Ability strings provide a compact behavioral sketch:

| Field | Use | Limitation |
| --- | --- | --- |
| `Name`, `Tip`, `Hotkey` | command-card presentation | does not prove order registration |
| `Ubertip` | visible effect, target class, level fields | may omit edge cases and implementation details |
| `Untip`, `Unubertip` | toggle/autocast inverse | does not define status cleanup |
| `Researchtip`, `Researchubertip` | hero learning presentation | may describe a related campaign variant |
| `Bufftip`, `Buffubertip` | player-visible status | does not define stacking or dispel rules |
| `<rawcode,Field>` | authored value dependency | field units still come from the normalized SLK row |
| `Globalmessage`, `Globalsound` | visible cast/death feedback | empty values are meaningful and should remain empty |

Do not treat a tooltip as a complete target predicate. For example, “enemy land
units” establishes the ordinary War Stomp filter, but immunity, summoned units,
collision radius, stun replacement, and timing still require a runtime contract or
observation.

Campaign entries can be separate object-data variants. `AOw2` in the campaign table
is not automatically the same implementation record as standard Orc `AOws`; confirm
the rawcode and active `AbilityData.slk` row before sharing a handler.

## Implementation Triage

Start with abilities whose behavior maps directly to an existing runtime contract:

| Category | Typical contract | First implementation strategy |
| --- | --- | --- |
| direct damage/heal | `ability_t` with `AB_SPELL` | apply the authored amount to validated targets |
| timed buff/debuff | `unit_addtimedstatus` | add and expire the status through normal lifecycle code |
| toggle | `AB_TOGGLE` plus status | execute the same command to add/remove the state |
| passive aura | ability modifier hooks consumed by movement/combat | calculate from active nearby owners; avoid stale recipient state |
| attack proc | ability procedure at attack resolution | evaluate chance and effect in the owning ability at damage time |
| autocast | autocast messages | acquire targets, then issue the ordinary spell order |
| channel/wave | `AB_CHANNEL` and thinker | schedule ticks and cancellation explicitly |
| summon | summon helpers and unit rawcode | use authored `UnitID`/`Data` values and duration |
| morph/transform | unit-type rebinding contract | preserve the edict and restore all affected unit state |
| cargo/inventory | cargo or item lifecycle | use the existing handle and ownership rules |

Regeneration auras follow this model: `Aoar`/`Aabr`/`Aarm` are passive base handlers, while custom/stock aliases retain their own `Area`, `targs`, `DataA`, and `DataB` through `AbilityData.code` normalization. See [Regeneration Auras And Fountains](regeneration-auras.md).

Do not force passive, attack, aura, or autocast abilities into a visible cast handler
just because their object-data class is named `CAbility*`. The runtime contract owns
where the effect is evaluated.

Ability procedures use TFT `CAbility*` class names. Look up the FourCC in
`tft-ability-classes.txt` and name the function after its class: `CAbilityDoom`,
`CAbilityWarStomp`, `CAbilityFrostArmor`. Do not invent names or derive them
from rawcode abbreviations. Procedure macros prepend `C` to a PascalCase argument;
`BZ_SIMPLE_SPELL_PROC(AbilityDoom) { ... }` defines `CAbilityDoom` and its `AbilityDoom_Execute` body,
whose parameters are `caster`, `st`, and `spell`.
The rawcode-to-procedure mapping and all static flags/target/order data live only in
`abilitylist` in `s_skills.c`. `S_AbilityItem(actual_code)` produces an `abilityitem_t` with the requested
rawcode and resolved registry row. Validation and execution receive this item, so two aliases can share a
procedure while reading different authored data. There is no canonical code assignment or `.alias = true` rule.
Display text is fetched through authored ability profiles/strings and map overrides; gameplay values use
`G_AbilityData` / `G_AbilityLevel` by the actual rawcode.

Register concrete `AbilityData.alias` row IDs and `AbilityData.code` implementation IDs, plus necessary internal
commands. Do not register the abstract TFT class tree. See [behavior and identity](ability-inheritance-plan.md#rawcode-and-procedure-identity)
and [data-backed registry](ability-inheritance-plan.md#data-backed-registry) for lookup and verification details.


The active rawcode portion of `games/warcraft-3/game/skills/s_skills.c` is generated
from the `*AbilityStrings.txt` files in `data/strings`. It is grouped by source file,
uses the text entry's `Name` as its block comment, and preserves the existing handler
mapping. Regenerate and check it with:

```sh
python3 tools/generate_ability_registry.py --write
python3 tools/generate_ability_registry.py
```

The generator uses the first sorted text file when a rawcode is defined more than
once. Entries without a text definition remain in the final `No AbilityStrings source
file` group. The commented TODO entries are also generated, remain disabled, and are
delimited by their own generated markers.

## Adding a New Ability

### 1. Identify the ability

Poll the ability first (see [Mechanical Workflow](#mechanical-workflow)):

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw ANdo
```

The brief already prints `class:` / `parent=` from `tft-ability-classes.txt`. Confirm
registry status only if the brief is not enough:

```sh
python3 tools/wc3_ability_class_audit.py --format=todo | rg ANdo
```

### 2. Identify shared behavior

The retail parent is evidence for behavior to reproduce through flags and explicit procedure delegation.
It does not determine our runtime memory layout. Common retail families:

| Parent | Category | Shared behavior |
| --- | --- | --- |
| `CAbilitySimpleSpell` (AAsm) | Most hero/unit spells | mana cost, cooldown, targeting, `spell_cmd` pipeline |
| `CAbilityAutoTargetSpell` (AAat) | Autocast spells (Heal, Slow) | autocast hooks + spell pipeline |
| `CAbilityRangerArrow` (AHRa) | Toggle attack arrows | autocast + toggle status |
| `CAbilityPassive` (APas) | Passive abilities | no command handler; consumed by combat/movement |
| `CAbilityMorph` (Amor) | Morph abilities | unit-type rebinding |
| `CAbilityAura` (aura) | Aura abilities | persistent nearby-unit effects |
| `CAbilitySpell` (AAsp) | Channel/toggle/complex spells | mana + cooldown but not simple-spell targeting |
| `CAbilityPersistentBonus` (APbo) | Item stat bonuses | passive bonus applied/removed on equip |
| `CPower` (powr) | Fundamental abilities (Move, Attack) | always present on units |

Before implementation, record the expected inputs, target rules, timing, result, and inverse/cleanup behavior from
the data. Add the smallest representative fixture and a failing test through production dispatch for missing or
incorrect behavior. See [Testing](#testing); rawcode registration alone is not a behavioral test.

### 3. Define the ability

Work in the owning `games/warcraft-3/game/skills/s_*.c` file and include `s_skills.h`.
Choose the macro by the messages the behavior needs:

| Form | Body parameters / return | Dispatch contract |
| --- | --- | --- |
| `BZ_SIMPLE_SPELL_PROC(AbilityName) { ... }` | `caster`, `st`, `spell`; `void` | `A_EXECUTE` calls the body and returns true; other messages delegate to `CAbilitySimpleSpell` |
| `BZ_COMMAND_PROC(AbilityName) { ... }` | `clent`; `void` | handles `A_COMMAND`; other messages return false |
| `BZ_ITEM_PROC(AbilityName) { ... }` | `clent`; `BOOL` | handles `A_ITEM_USE`; body success permits charge consumption; other messages return false |
| `BZ_ABILITY_PROC(CAbilityName) { ... }` | `ent`, `msg`, `call`; `intptr_t` | explicit switch for custom validation, lifecycle, orders, updates, or other messages |

The three body macros take **one name argument**. They generate a public `CAbilityName` procedure and a
file-local helper (`AbilityName_Execute`, `AbilityName_Command`, or `AbilityName_ItemUse`). The macro supplies
both the helper's static forward declaration and its definition header. Do not add a separate helper declaration,
manual signature, second callback argument, or semicolon between the macro and its body. Use `AbilityName`
without the leading `C` for these macros; `BZ_ABILITY_PROC` takes the full `CAbilityName`.

For a spell that uses the shared pipeline:

```c
/* Name=Doom
 * Ubertip="Curses a target enemy unit, dealing damage over time."
 */
BZ_SIMPLE_SPELL_PROC(AbilityDoom) { target_status_execute(caster, st, spell); }
```

Put single-use execution logic directly in the body; keep a named helper only for behavior shared by multiple
procedures. If rawcodes have identical behavior and dispatch policy, point their registry rows at one procedure.
For example, `AUfa` and `AUfu` both use `CAbilityFrostArmor`; there is no separate `CAbilityFrostArmorAuto`
wrapper. Keep each requested rawcode in `abilityitem_t.code` so authored data remains distinct. A shared
procedure does not itself add autocast support; that requires the appropriate policy and message handlers.
The macro forward-declares `AbilityDoom_Execute` and routes `A_EXECUTE` to it. Flags and target shape belong
in the registry row. For behavior that needs more messages, use `BZ_ABILITY_PROC` (which supplies `ent`, `msg`,
and `call`), write the switch directly, and delegate unhandled messages to the TFT parent procedure.

See [Holy Light's complete procedure](../../../games/warcraft-3/game/skills/s_holylight.c) for custom
`A_VALIDATE`/`A_EXECUTE` cases and explicit parent delegation. Read `call`'s union member only inside the
corresponding message case, handle absent payloads as required, and never retain borrowed payload pointers.
A handled false result is final; delegate only messages the procedure does not handle.

`BZ_VALIDATED_SPELL_PROC(NAME, VALIDATE, EXECUTE)` still takes three arguments and defines the whole
procedure. It remains available for existing shared validation/execution helpers; it does not take a trailing
body like the three one-argument macros. Use an explicit procedure when additional message handling is needed.

Command abilities use the same body syntax. `BZ_COMMAND_PROC(AbilityMove) { ... }` forward-declares
`AbilityMove_Command(LPEDICT clent)`, routes `A_COMMAND` to it, and supplies its function header.
`clent` is `call->client` when supplied, otherwise the procedure's `ent`. Other messages return false.
Use the generated `AbilityMove_Command` name for same-file direct calls or menu callbacks. For example,
`AbilityBuild_Command` installs itself as `client->menu.refresh`. These helpers are file-local; cross-module
ability dispatch goes through the procedure contract.

```c
BZ_COMMAND_PROC(AbilityCancel) { CMD_CancelCommand(clent); }
```

Item use follows `BZ_ITEM_PROC(AbilityItemHeal) { ... }`. It forward-declares
`BOOL AbilityItemHeal_ItemUse(LPEDICT clent)` and supplies that function header. `A_ITEM_USE` passes the
procedure's `ent` as `clent`; the body returns whether use succeeded so inventory can consume a charge.
Other messages return false without running the body. See [item implementations](../../../games/warcraft-3/game/skills/s_item.c)
for success and rejection paths. Unlike the command macro, the item macro passes `ent` directly and does not
select `call->client`. Preserve false returns when the effect cannot apply, such as healing a full-health unit.

### 4. Declare and register

Add an extern declaration to `s_skills.h`:

```c
BZ_ABILITY_PROC(CAbilityDoom);
```

Add the concrete row to `abilitylist` in `s_skills.c`, preserving its authored rawcode:

```c
{ "ANdo", CAbilityDoom, AB_SPELL, SPELL_TARGET_UNIT },  /* Doom */
```

When promoting an inactive `// TODO:` entry, add a complete active row with the procedure, flags, and target
shape; the TODO line is an inventory entry, not a ready-to-uncomment initializer. Register concrete `AbilityData`
aliases/implementation codes and necessary internal commands, not abstract TFT helper classes.
Regenerate the AbilityStrings grouping after editing the active mapping, then run its check mode:

```sh
python3 tools/generate_ability_registry.py --write
python3 tools/generate_ability_registry.py
```

These commands require the extracted `data/strings/*AbilityStrings.txt` inputs. The generator preserves your
handler mapping and removes corresponding inactive TODO entries; it does not implement or choose behavior.

### 5. Select command policy

Use `AB_SPELL` for the shared cast processor and handle `A_EXECUTE` plus optional `A_VALIDATE` in the procedure.
Combine independent policies (`AB_CHANNEL`, `AB_TOGGLE`, `AB_AUTOCAST`) in the registry row. Bespoke orders use
`AB_COMMAND`/`A_COMMAND`; consumable item effects use `AB_ITEM`/`A_ITEM_USE`.
For named immediate orders, supply `ability_t.orders` and handle `A_ORDER`. For persistent per-unit work,
register `AB_UPDATE` and handle `A_UPDATE`. Lifecycle messages belong in the owning procedure.
`S_AbilityHasCommand` and `S_AbilityCommand` provide the common HUD, player and item entry contract.

Concrete relatives can call the same helper or parent procedure: Fire Bolt and Thunder Bolt share effect code
while the per-use `abilityitem_t` carries the actual rawcode. There is no runtime parent wiring.

Corpse-fed no-target channels keep acquisition and periodic behavior in their ability procedure. `Acan`
(Cannibalize) selects the nearest dead, non-Hero, organic unit within `DataB`, consumes that corpse when the
cast commits, then restores `DataA` health once per second for `Dur`. The shared `AB_CHANNEL` lifecycle owns
movement/death interruption; the ability-owned thinker owns pulse timing and ends the channel after the final
authored-duration pulse. ROC and TFT both author `DataA=10`, `DataB=800`, and `Dur=33` for stock `Acan`.

### 6. Write tests

Write focused tests before launching the game. See [Testing](#testing) for the required
coverage: registration lookup, authored data mapping, positive path, nearest negative
path, duration/expiry, and save/load.

### 7. Verify coverage

```sh
python3 tools/wc3_ability_class_audit.py --format=coverage
python3 tools/wc3_ability_class_audit.py --format=hierarchy | rg -A2 ANdo
```

The audit verifies active registration against the extracted TFT reference. It does not prove behavior,
correct procedure sharing, or autocast support. Check both the registry mapping and focused gameplay assertions.

```sh
make game
make test-wc3-engine WC3_PATTERN='wc3_spell.*'
make test
```

The focused WC3 target runs the generated fixture archive in both classic and TFT modes. Choose the suite that
covers the actual behavior (`wc3_unit.*`, inventory, or another owner) as needed; finish with the full suite.

## Behavior Record

Before writing the handler, create a short record in the relevant source or test:

For an implemented handler, put the matching string-table contract immediately
above the function or ability registration as a comment. Use the exact `Name` and
`Ubertip` text when it is short enough to remain readable; preserve placeholders
such as `<AOws,DataA1>` and `<AOws,Dur1>` so the comment stays tied to authored
data rather than hardcoding a value. For toggle/autocast abilities, also include
`Untip` and `Unubertip` above the inverse command path. These comments are an
implementation index, not a replacement for the source strings or a claim that
every tooltip sentence is fully implemented.

Example:

```c
/* Name=War Stomp
 * Ubertip="Slams the ground, dealing <AOws,DataA1> damage to nearby enemy land units and stunning them for <AOws,Dur1> seconds."
 */
BZ_SIMPLE_SPELL_PROC(AbilityStomp) { /* ability-owned implementation */ }
```

For an inverse/toggle path:

```c
/* Untip=Stop Defend
 * Unubertip=""
 */
```

Do not infer toggle semantics from `Untip` alone. Retail Avatar is a timed
one-shot spell whose `BHav` buff owns its lifetime.

If a handler is shared by several rawcodes, list each rawcode's string contract
or state explicitly which entries share the implementation. If the entry is not
implemented, annotate the registry entry with `TODO` instead of copying a
tooltip above an unrelated function.

```text
rawcode:       AOws
archive:       ROC/TFT row checked separately
strings:       OrcAbilityStrings.txt entry and relevant tooltip fields
class:         authored class or unresolved
state:         instant, toggle, passive, autocast, channel, or morph
inputs:        Area, DataA, Dur, and any other consumed fields
relationship:  target ownership, alive state, unit type, immunity rules
side effects:  damage, status, movement, summon, item, or presentation changes
timing:        cast point, impact, tick, duration, cooldown, inverse
confidence:    confirmed, observed, inferred, or unresolved
next check:    the smallest test or observation that could disprove it
```

An inferred field is acceptable when the record says it is inferred and a focused
test covers both the expected path and the nearest negative path. Do not silently
turn a tooltip guess into a general engine rule.

## Presentation And Animation

Ability strings do not normally specify model animation. Resolve presentation in
layers:

1. Check the unit model's available sequences.
2. Check `UnitProfile.uani` / `animProps` Required Animation Names.
3. Reuse the logical animation request system (`spell`, `morph`, `stand`, `attack`)
   instead of hardcoding a model-specific sequence.
4. Check authored ability art, effect, sound, and `AnimSounds.slk` data.
5. Observe timing in retail if the effect must line up with an animation event.

A missing animation string in `Game.dll` does not prove that no animation is played.
The choice may be made by shared casting code, an animation enum, unit data, or the
model itself. Disassembly is useful here only when observation and model/data inspection
cannot establish the required timing or state transition.

## Example: War Stomp

The standard Orc War Stomp entry describes an instant self-centered effect:

- damage nearby enemy land units;
- stun those units;
- use `DataA` for damage and `Dur` for stun duration;
- use `Area` for the radius;
- expose the normal War Stomp name and hotkey.

The local `AOws` handler in `games/warcraft-3/game/skills/s_ability_stubs.c` already
implements that gameplay core through `spell_cmd`, `S_SpellData`, `S_SpellNumber`,
`S_SpellDuration`, relationship checks, and the timed stun status.

## Current Campaign Audit

The campaign strings are not interchangeable with standard ability rawcodes. A
name match is not enough: the registry lookup uses the rawcode, and the campaign
rows must be checked separately in `AbilityData.slk`.

Confirmed from `games/warcraft-3/game/skills/s_skills.c` and
`data/strings/CampaignAbilityStrings.txt`:

| Campaign entry | Registry status | Result |
| --- | --- | --- |
| `AOw2` War Stomp | `CAbilityWarStompCampaign` | campaign procedure reads its requested rawcode |
| `ANsh` / `AOs2` Shockwave | `CAbilityShockwaveCampaign` / `CAbilityShockwaveCairne` | shared campaign area-damage helper, separate rawcodes |
| `ANcf` Breath of Fire | `CAbilityBreathOfFireCampaign` | shared campaign area-damage helper |
| `Acdh`, `ANhw`, `ANhx` | campaign Drunken Haze, Healing Wave, and Hex procedures | currently share `campaign_status_execute`; verify each gameplay contract |
| `ACs7`, `ACs8`, `Arsq`, `Arsg`, `Arsp`, `Acef`, `Arsw`, `AOls` | campaign summon procedures | share `campaign_summon_execute` with authored rawcodes |
| `ANbr`, `ANsb` | `CAbilityBattleRoar`, `CAbilityStormBoltCampaign` | dedicated execution bodies |
| `AOr2`, `AOr3` | `CAbilityEnduranceAuraCampaign`, `CAbilityReincarnationCairne` | currently share `campaign_toggle_execute`; verify passive/lifecycle requirements |

These are registration and code-path observations, not a completeness claim. Verify the behavior in
`skills/s_campaign_abilities.c` with focused tests before consolidating a campaign row with a standard procedure.

## Human Ability Audit

The Human registry entries audited from `HumanAbilityStrings.txt` are owned by
`skills/s_human_abilities.c`. ROC and TFT currently provide identical normalized rows
for the selected entries. Active spells use the shared `spell_cmd` path, and Heal,
Inner Fire, Slow, and Spell Steal use the generic one-enabled-ability autocast state in
`edict_t.autocast_code`.

| Runtime owner | Abilities | Authored inputs consumed |
| --- | --- | --- |
| unit-target spell | Aerial Shackles, Control Magic, Cloud, Inner Fire, Heal, Slow, Invisibility | `Rng`, `Dur`/`HeroDur`, `BuffID`, `DataA-C` |
| timed morph | Polymorph (`Aply`) | `Ply1`/`DataA`, `Ply2`-`Ply5`/`DataB-E`, `Rng`, `Dur`, `BuffID`, target mask |
| point spell/thinker | Flare, Dispel Magic | `Area`, `Dur`, `DataB` |
| toggle/status | Defend, Magic Defense | `DataA-F`, `Dur`, `HeroDur` |
| timed transformation | Avatar | `BHav`, `DataA-C`, `Dur` |
| attack resolution | Feedback, Flak Cannons, Fragmentation Shards, Barrage, Storm Hammers | `DataA-E`, `Area`, unit attack splash fields |
| capability/presentation marker | Sphere, Phoenix Morphing, Flying Machine Bombs, True Sight, Magic Sentry | registry presence; no invented cast behavior |

Central consumers apply Slow/Defend movement factors, Inner Fire armor, attack
locks, Defend piercing reduction/reflection,
Feedback mana burn, and attack splash. Invisibility clears on attacks, spell commits,
and status expiry. `human_ability_think` owns Flare reveal updates and Aerial Shackles
damage ticks and is appended to the save callback roster.

Polymorph keeps the original edict/class/stat identity and stores only reversible model, scale, and movement-speed presentation state. The configured `Ply2`-`Ply5` form supplies the temporary model/movement speed, `Ply1` gates Neutral Hostile creep level, and the configured timed buff owns expiry/dispel restoration. See [Polymorph](polymorph.md).

Avatar stores its applied armor, maximum-health, and attack-damage deltas on the
edict. `BHav` owns the lifetime; expiration, death, and ability removal subtract the
stored values, clamp current health, remove the `alternate` animation property, and
invalidate the info panel. Hero stat recomputation includes
`temporary_health_bonus`, so attribute changes cannot erase an active Avatar bonus.
Spell effects call `S_SpellDamage`, which rechecks `BHav` at impact time, while
physical attack damage continues through `T_Damage`.

Detection is a per-player visibility concern; True Sight and Magic Sentry must be
implemented in the snapshot/FOW visibility contract rather than by globally clearing
`RF_HIDDEN`.

Useful checks:

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Adef
make test-wc3-engine WC3_PATTERN='wc3_spell.*'
make test-wc3-engine WC3_PATTERN='wc3_save.*'
```
Registration does not establish that a handler meets every tooltip requirement. Campaign/standard sharing
requires focused tests for differences such as Breath of Fire's interaction with Drunken Haze.

The remaining questions should be handled independently:

- Does damage occur at command acceptance, cast point, or impact?
- Which units are excluded besides air units, allies, dead units, and the caster?
- Which logical caster animation is requested?
- Which effect and sound are authored, and when are they emitted?
- Does stun replace, refresh, or combine with an existing stun?

Use a focused test or retail observation for each question. Do not block the gameplay
implementation on unrelated questions.

## Disassembly: Optional Evidence

For the demo's actual FOURCC-to-class registrations, use the
[Python extractor and offset reference](demo-ability-classes.md). Its generated
mapping includes internal type IDs; it does not guess object-data aliases.

Use `Game.dll` when it answers a specific unresolved question, not as the default
implementation workflow. Good questions include:

- whether a cast applies an effect at cast start or at an animation callback;
- which shared helper enforces an unusual target predicate;
- how a morph preserves handles, inventory, buffs, or health;
- whether a campaign variant has a distinct lifecycle;
- which logical animation or sound event is selected when data is inconclusive.

Keep the binary digest and exact command when recording a result:

```sh
shasum -a 256 data/Warcraft3demo/Game.dll
rabin2 -I data/Warcraft3demo/Game.dll
r2 -q -e bin.cache=true -A \
  -c 'izz~CAbilityWarStomp' -c 'izz~AOws' -c 'q' \
  data/Warcraft3demo/Game.dll
```

Prefer call-site and behavior evidence over class names. If using radare2, verify
`pdg` against `pdf`; decompiler output is orientation only. Do not transplant an
address from the demo DLL into another build. Preserve unresolved binary findings as
notes attached to the specific ability rather than making them a prerequisite for
all ability work.

## Testing

Recreate the gameplay situation in automated tests before launching the game. Use production order/ability dispatch,
the real animation and update callbacks, deterministic simulation time, and representative archive fixtures. If the
intended reproducer is a sequence of player or script actions, test that sequence rather than only the final helper.
Extend the harness or fixture generator if it cannot yet exercise the path. See the
[test-first workflow](../../../CONTRIBUTING.md#test-first-behavior-verification).

Each new ability needs focused tests for:

- registration and rawcode lookup;
- the authored level and field mapping;
- the positive target/effect path;
- the nearest invalid target or inverse state;
- duration, cancellation, expiry, and repeated casts where applicable;
- save/load when the ability adds persistent entity state;
- ROC and TFT rows when the archives differ.

For Raven Form, reproduce both completion of Morph and replacement by Move before its end callback, followed by ascent,
pause, and reversal. Assert the retained order, form, animation interval, height, and cleanup at the relevant steps.
Exercise the generic scheduler as well as the ability callback so a missing registration or lifecycle hook cannot hide
behind a passing helper test. Use the generated morph model to select actual sequences; do not require a campaign replay
to prove these transitions. A save/load or parser bug belongs in the corresponding fixture-backed regression suite.

Run the affected WC3 tests first, then the full suite:

```sh
make test-wc3-engine WC3_PATTERN='wc3_unit.*'
make test-wc3-engine WC3_PATTERN='wc3_spell.*'
make test
```

Only use a bounded game run for a named property the tests and tools cannot measure, such as the final rendered pixels.
State that gap before launching and exercise it directly. Animation selection, effect lifetime, status cleanup, and
return to the normal state should already have automated assertions. Once covered, rerun the tests instead of the game.

### Cyclone (`Acyc`)

ROC and TFT `AbilityData.slk` define Cyclone as a one-level, 600-range unit spell with `DataA-C = 1,0,0`,
`Dur = 20`, `HeroDur = 6`, and `BuffID = Bcyc,Bcy2`. Its allowed targets are enemy organic air and ground units;
the TFT tooltip states the non-mechanical restriction explicitly. TFT registers `Acyc` as `CAbilityCyclone` with
`CAbilitySimpleSpell` (`AAsm`) as its parent, while `Bcyc` and `Bcy2` are `CBuffCyclone` and
`CBuffCycloneTwo` respectively.

OpenRealm stores the active authored buff in `abilstatus[]` and derives Cyclone's action and target locks from either
Cyclone buff rawcode. The concrete procedure validates the enemy/non-mechanical rule, applies the first authored
`BuffID` for `Dur` or `HeroDur`, and stops the target's current order. Generic Move, Attack, spell-caster, spell-target,
and damage paths consult the status predicate, so expiry restores behavior without an `edict_t` or network-state field.

## Known Pitfalls

- Do not parse `AbilityData.slk` with an ad-hoc script. `ability_audit -raw`
  already uses the same DDX schema as the game.
- Do not treat `-data 'data/Warcraft III'` as ROC. That directory contains
  `Frozen Throne/War3x.mpq`; first-wins MPQ search returns the TFT row unless
  `-roc` opened War3.mpq alone.
- Do not treat `code=` aliases as identical behavior. `Aams` and `Aam2` share
  `CAbilityAntiMagicShell` and `BuffID=Bams,Bam2`, but empty DataC applies `Bams`
  (cannot be targeted) and DataC=300 applies `Bam2` (spell-damage pool). The
  Buffubertip, not the comments column, states that split.
- ROC may omit `BuffID` while `*AbilityStrings.txt` still names the buff. Document
  the fallback; do not invent DataC that the ROC row does not have.
- `BuffID=Bams,Bam2` is two tokens. `unit_addtimedstatus` reads only the first
  four characters of the string you pass it; pick the token the brief says to apply.
- `targs` without `friend`/`enemy`/`neutral` allows any living air/ground unit,
  including enemies. `Aam2` adds `friend,self` and must reject enemies.
- `TEST(wc3_spell, foo_bar)` is matched by `wc3_spell.foo_bar` / `wc3_spell.foo*`,
  not `wc3_spell.foo.*`.
- `S_SpellIsEnemy` is false when `mapinfo->players[n].playerType` is
  `kPlayerTypeNone`. Set Human (or another real type) in the fixture.
- Do not assume a campaign rawcode is interchangeable with its standard counterpart.
- Do not expose a passive ability as a command merely because it has a `CAbility*`
  class name.
- Do not hardcode per-level values that already exist in `AbilityData.slk`.
- Do not silently ignore an unresolved effect, sound, target rule, or animation;
  record it and add the smallest check that will resolve it.
- Do not copy disassembly addresses between DLL builds.
- Do not treat Warsmash or another engine as recovered Blizzard source. Use it as an
  independent behavioral reference.

See also:

- [Ability coverage](architecture/ability-coverage.md)
- [Anti-Magic Shell](anti-magic-shell.md) — worked example of the `-raw` brief and DataC/BuffID split
- [Unit animation properties](unit-animation-properties.md)
- [Ability and item effects](ability-and-item-effects.md)
- [Warcraft III data model](../../wc3-data-model.md)
- [Diagnostic tools](../../diagnostic-tools.md#ability-brief-ability_audit)

## Ability-owned orders and persistent behavior

This is the direction for new gameplay work and for refactoring behavior encountered in general-purpose files.
Split behavior by the ability that owns it; do not grow `g_monster.c`, `m_unit.c`, or `g_ai.c` with individual spell rules.
Keep an ability's order strings, validation, state transitions, animation moves, completion functions, and timed effects in its
`skills/s_*.c` owner. For immediate orders outside the spell pipeline, register `ability_t.orders` and handle `A_ORDER`;
for effects that outlive an active order, register `AB_UPDATE` and handle `A_UPDATE`. `s_skills.c` owns generic dispatch and deduplicates shared
update handlers at initialization. Do not add a spell-name branch or direct spell update to `m_unit.c`/`g_monster.c`.
See [Raven Form](unit-animation-properties.md) for the order/update contract and persistence tests.
Unit-data behaviors use `AB_INNATE` and `S_UnitAbilityEvent` for spawn/rebind, idle acquisition,
move interruption, damage, entity removal, and automatic-target eligibility. Each message carries
an `abilityitem_t`; the owner decides whether its per-unit state applies. See
[Creep Sleep](creep-sleep.md) for `ACsp` and its direct `CAbilitySleepAlways` parent delegation.

Encapsulation includes setup, interruption, cancellation, inverse orders, expiry/death/removal cleanup, and restoration
after save/load where applicable. Moving a command function into `skills/` while leaving its timer or cleanup in a generic
unit loop is incomplete. Use existing lifecycle hooks or add a small generic hook when necessary. The shared mechanism
dispatches or applies an ability's result; the ability decides its own rules. Keep this flat and data-oriented.

`skills/s_move.c` owns reusable locomotion (steering, steps, collision policy, route goals, and support height).
Attack, Follow, Harvest, and Build call that ability's movement operations while owning their own goals and arrival
conditions. `g_ai.c` owns acquisition/behavior transitions, and `g_monster.c` owns initialization and generic animation dispatch.
Shared math, routing algorithms, collision queries, serialization, and in-place type rebinding remain reusable services;
ability ownership does not mean duplicating these mechanisms in each ability.
