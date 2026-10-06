# Flat C Ability System

Status: implemented, 2026-09-12. This document records the representation and identity decisions behind the
[ability message procedure system](ability-procedures.md).

OpenWarcraft3 uses a flat, Quake-style registry plus explicit procedure delegation. It does not reproduce the
TFT C++ object layout, constructor chain, virtual table or runtime parent graph. The extracted TFT hierarchy is
an offline behavioral reference: it supplies concrete procedure names and identifies useful shared behavior.

## Decision

Each active rawcode or internal command has one `ability_t` registry row:

```c
static ability_t abilitylist[] = {
    { "AHhb", CAbilityHolyBolt, AB_SPELL, SPELL_TARGET_UNIT },
    { "Arav", CAbilityRavenForm, AB_COMMAND | AB_UPDATE, SPELL_TARGET_NONE, raven_orders },
};
```

The row owns static dispatch data: classname, procedure, flags, target shape and optional order names. The
`CAbility*` identifier is a function with the common `abilityProc_t` signature. Behavior state lives on edicts
and thinkers, never in the registry row.

The TFT parent is expressed by an ordinary call for unhandled messages. For example, `CAbilityHolyBolt` handles
validation and execution, then delegates its default switch arm to `CAbilitySimpleSpell`. This is the same shape
as a Win32 window procedure calling its default procedure. There is no runtime inheritance metadata.

## Quake 2 analogy

The relevant Quake 2 pattern is a compact data record selecting a shared processor and concrete behavior:

| Quake 2 pattern | WC3 equivalent |
|---|---|
| `gitem_t` flags and behavior functions | `ability_t` flags and `abilityProc_t` |
| `Weapon_Generic` shared state machine | shared spell command/cast processor |
| concrete weapon fire function | concrete `CAbility*` message cases |
| `movetype` selecting physics policy | target-shape enum and independent ability flags |

Flags represent independent policy, not ancestors. `AB_SPELL`, `AB_CHANNEL`, `AB_TOGGLE`, `AB_AUTOCAST`,
`AB_COMMAND`, `AB_ITEM` and `AB_UPDATE` tell consumers which shared path or optional messages apply. They do not
encode `CAbility -> CAbilitySpell -> CAbilitySimpleSpell` as a bit hierarchy.

## Rawcode and procedure identity

Procedure identity and authored ability identity are different. Several registry rows may use the same procedure,
but every invocation keeps its actual rawcode:

```c
typedef struct {
    DWORD code;
    ability_t const *ability;
} abilityitem_t;
```

`S_AbilityItem(code)` resolves the behavior through `AbilityData.code` while preserving the requested code.
Validation, execution and shared delegates receive that item in `abilityCall_t`. Costs, rank data, effects,
buffs, strings and cooldown grouping therefore use the authored rawcode rather than reverse-mapping a shared
procedure to an arbitrary canonical row.

Procedure pointers are suitable for ownership comparisons and update deduplication, but not for mutable state,
save identity or cooldown identity. Save/load persists rawcodes and relocatable ordinary callbacks; it does not
serialize registry pointers.

## Data-backed registry

The ROC and TFT AbilityData tables contain both row aliases and implementation codes. Register concrete values
from those columns plus necessary internal `Cmd*` entries. Never register abstract retail helpers such as `abil`,
`AAsp` or `AAsm`; shared procedures are called directly and are not playable rows.

The active section of `skills/s_skills.c` contains implemented rows. The generated TODO section is an inactive
coverage inventory, not registry storage. Passive rows may use `CAbilityPassive` when their gameplay is consumed
by an owning combat, movement or lifecycle subsystem and they require no procedure message of their own.

Use the audits to verify the boundary:

```sh
python3 tools/wc3_ability_class_audit.py --format=coverage
python3 tools/check_wc3_ability_registry.py --mpq "data/Warcraft III/War3.mpq" \
    --mpq "data/Warcraft III/Frozen Throne/War3x.mpq" --check-active
```

The class audit distinguishes active concrete rows, inactive TODO rows and unregistered retail helper classes.
Registration is not proof of gameplay completeness; each active behavior still needs authored-data and observable
effect tests.

## Dispatch ownership

The ability module owns target validation, effects, immediate orders, state transitions, animation moves,
persistent updates, inverse operations, interruption and cleanup. Generic command, unit and AI code resolves a
row and sends a message. It must not call spell-specific update functions or branch on individual rawcodes.

The shared spell path performs universal resource, cooldown, range, target and cast-phase work. It sends
`A_VALIDATE` and `A_EXECUTE` to the resolved concrete procedure. A false handled result is final; only an explicit
default arm delegates to a parent procedure. Borrowed call payloads are never retained across the synchronous call.

Animation completion and thinker functions remain ordinary callbacks when they represent asynchronous lifecycle
work. `umove_t.proc` identifies the ability procedure that owns a move without turning every animation callback
into an ability message.

## Verification

Focused tests cover registry metadata, aliases sharing a procedure, per-use rawcode data, Holy Bolt delegation,
commands, items, autocast, membership, levels, persistent updates and move ownership. The full WC3 suite also
covers save/load and the existing gameplay behavior migrated from the former representation.

```sh
make openwarcraft3-tests
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_spell.*'
python3 -m unittest tests/test_check_wc3_ability_registry.py
make test
```
