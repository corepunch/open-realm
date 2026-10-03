# Ability Message Procedures

Status: implemented, 2026-09-12. All active WC3 abilities use one procedure per behavior. The old callback-table
representation and migration bridge are gone.

The design follows the Win32 window-procedure model: a concrete `CAbility*` procedure handles messages in a
switch and explicitly calls a shared procedure for messages supplied by its TFT parent behavior. The TFT class
registry is an offline guide to those relationships; there is no runtime parent pointer or inherited object.

## Registry and call contract

The static registry contains the actual ability ID and all static dispatch metadata:

```c
typedef struct ability_s ability_t;
typedef struct ability_call_s abilityCall_t;
typedef intptr_t (*abilityProc_t)(LPEDICT ent, abilityMsg_t msg, abilityCall_t const *call);

struct ability_s {
    LPCSTR classname;
    abilityProc_t proc;
    DWORD flags;
    spellTargetType_t target_type;
    LPCSTR const *orders;
};
```

A row is therefore self-contained:

```c
static ability_t abilitylist[] = {
    { "AHhb", CAbilityHolyBolt, AB_SPELL, SPELL_TARGET_UNIT },
    { "Arav", CAbilityRavenForm, AB_COMMAND | AB_UPDATE, SPELL_TARGET_NONE, raven_orders },
};
```

`ability_t` is registry data, not behavior storage. `CAbilityHolyBolt` and `CAbilityRavenForm` are functions.
Aliases may share a procedure but retain separate rows so authored rawcode identity is never lost.

`abilityitem_t` resolves a registry row together with the actual rawcode used by the unit or item. This matters
for map-defined aliases: behavior can be shared while AbilityData, rank, costs, effects and presentation remain
attached to the authored rawcode.

The procedure signature uses `abilityCall_t` instead of literal `WPARAM` and `LPARAM`. Its message-selected
union preserves pointer width and gives callers named, typed fields:

```c
struct ability_call_s {
    abilityitem_t const *item;
    union {
        spellTarget_t const *target;
        LPEDICT client;
        LPCSTR order;
        LPCSTR classname;
        DWORD level;
        BOOL enabled;
    };
};
```

Calls are synchronous. Payload pointers are borrowed for the duration of the call and must not be retained.
Delayed work stores the rawcode and entity state, then resolves the current registry row when it runs.

## Messages

| Message | Input/result contract |
|---|---|
| `A_INIT` | `classname`; initialize behavior-owned shared constants |
| `A_COMMAND` | command-source client; nonzero when handled |
| `A_TOGGLE_ON` | owning unit; nonzero when its alternate command is active |
| `A_VALIDATE` | `item` and `target`; nonzero when the target is valid |
| `A_EXECUTE` | `item` and `target`; nonzero when the effect executes |
| `A_ITEM_USE` | item/client entity; nonzero only when the effect applies |
| `A_AUTOCAST_ON` | owning unit; nonzero when autocast is enabled |
| `A_AUTOCAST_SET` | `enabled`; set autocast state |
| `A_AUTOCAST_ACQUIRE` | owning unit; nonzero when acquisition issues an action |
| `A_ENABLE`, `A_DISABLE` | ability membership notifications |
| `A_LEVEL` | owning unit; current behavior-specific level |
| `A_LEVEL_CHANGED` | `level`; rank-change notification |
| `A_ORDER` | `order`; nonzero when accepted |
| `A_UPDATE` | persistent per-unit behavior tick |

Flags, target shape and supported order names are read directly from `ability_t`; there are no query messages
for registry metadata. `AB_SPELL`, `AB_COMMAND`, `AB_ITEM`, `AB_UPDATE`, `AB_CHANNEL`, `AB_TOGGLE` and
`AB_AUTOCAST` declare the paths that may send the corresponding messages.

## Delegation

A concrete procedure handles only its own behavior and delegates unhandled messages to its shared parent:

Define it with `BZ_ABILITY_PROC(CAbilityHolyBolt)`, which supplies `ent`, `msg`, and `call`.
[Holy Light's implementation](../../../games/warcraft-3/game/skills/s_holylight.c) handles `A_VALIDATE` and
`A_EXECUTE`, reads `call->target` within those cases, and ends its switch with:

```c
default: return CAbilitySimpleSpell(ent, msg, call);
```

For simple execution, command, or item-use bodies, use the one-argument macros documented in
[Adding a New Ability](ability-implementation-plan.md#adding-a-new-ability). They generate file-local helpers
and the public message procedure; no separate callback argument or manual function header is needed.

Delegation is based on the TFT inheritance reference. For example, `AHhb` maps to `CAbilityHolyBolt`, whose
retail parent is `CAbilitySimpleSpell`; the leaf therefore calls `CAbilitySimpleSpell` for unhandled messages.
Shared procedures are not playable registry entries and abstract retail IDs such as `AAsm` are never registered.

A handled false result is final. Dispatch must not infer “unhandled” from zero because zero is also a valid
validation rejection, inactive toggle, failed item use or level result. Only the `default` switch arm delegates.
The original `call->item` is passed through every delegation level so a shared procedure still sees the actual
rawcode and registry row.

## Shared mechanic families

Concrete ability procedures remain the owners of Warcraft-specific policy, but repeated mechanics belong in shared helpers when their contracts are identical. A family helper must take authored identity (`abilityitem_t.code` or an explicit rawcode) and policy parameters rather than infer behavior from a hard-coded ability name. Do not replace the flat procedure model with a generic runtime ability-object hierarchy.

The first shared acquisition family is `S_AutocastAcquireUnit()`. Ordinary unit-target autocast abilities supply whether they want friendly or enemy candidates, whether candidates must be wounded, and the fallback acquisition radius. The helper then performs the common live-unit scan, authored target-mask check, nearest-candidate selection, and calls `S_CastUnitTargetSpell()` so mana, cooldown, validation, spell events, and execution stay on the normal cast path. Human, Undead, and general melee spell procedures use this one implementation. Abilities with genuinely different acquisition contracts (for example Barkskin's existing-buff exclusion or Moon Well's Area/threshold rules) keep their specialized acquisition code.

The combat-aura cache in `s_hero_passives.c` is the shared resolver for authored
alias/rank, range, target masks and strongest non-stacking numeric contributions.
Endurance Aura now uses that same resolver for both movement and attack speed
instead of rescanning all entities independently in `s_move.c` and `s_attack.c`.
Its DataA/DataB percentage authoring is normalized inside the shared resolver;
consumers receive fractional bonuses just like the existing
Unholy/Brilliance/Devotion family consumers. Slow Aura also uses the same cache:
cache keys declare whether a contribution comes from friendly or enemy sources,
so hostile movement/attack reductions no longer need their own entity scans. The
consumer keeps Slow Aura's 90% reduction clamp.

Corpse revival likewise has one death-state teardown primitive in `m_unit.c`.
`G_ReviveCorpse()` restores an ordinary permanent unit and reactivates food;
`G_ReviveCorpseAsSummon()` performs the same identity-preserving teardown for
temporary raised units while keeping them out of food accounting and marking the
consumed corpse unraisable/no-decay. Resurrection and Animate Dead therefore
differ in post-revival policy without duplicating decay/order cleanup.
`G_CorpseUnitLevel()` is the shared corpse-value accessor used by both the
high-level Resurrection/Animate Dead preference and Raise Dead's inverse
low-level preference; selection order remains ability-owned.

Use the same rule for future consolidation:

- target/projectile families share cast, target and impact machinery but keep ability-specific effects;
- channel families share channel ownership/interruption and thinker lifecycle but keep tick policy;
- aura families share source/recipient reconciliation, including friendly-vs-hostile source relation, but keep the numeric modifier consumer and any final clamp;
- summon families share unit creation/timed-life ownership but keep recast, replacement and corpse policies; simple `UnitID` + DataA count + Dur summons use `S_SummonAbilityUnits()`;
- corpse families share eligibility/reservation/revival primitives but keep ownership and post-revival policy;
- timed status families share lifecycle/presentation only when their duration and resistance semantics are identical. `S_SpellBuffId()` centralizes validation of an authored primary BuffID while each owner retains ROC/TFT fallback policy; `S_SpellHeroDuration()` and `S_SpellResistantDuration()` make the two existing duration-selection policies explicit without a mode flag. `S_SpellApplyTimedStatus()` is the presentation-neutral primitive: it applies/replaces a timed status and returns the authoritative slot so special families such as Disease Cloud, Ensnare/Web, Entangling Roots, Incinerate, Cyclone, and item invisibility can attach payload without rescanning `abilstatus`. `S_SpellApplyTimedTargetStatus()` layers standard authored TargetArt on that primitive for ordinary target buffs. `S_SpellBuffToken()` centralizes ordered comma-separated BuffID selection without deciding which token an ability wants. Simple persistent on/off statuses use `S_ToggleUnitAbilityStatus()`, while specialized toggles such as Defend retain their animation/expiry wrapper.
- standard stun users share `S_SpellApplyStun()`, which owns only the retail `Bstu`/level-one status contract; each attack or spell still owns hit validation and the exact normal, Hero, or resistant duration it passes in.
- simple fixed-damage point/radius nukes may use `S_SpellDamageEnemiesInRadius()` when their existing contract is exactly “living enemy units within radius.” Ground-only, cone, authored-target-mask, capped-damage, status-bearing, and structure/destructable variants keep their own enumeration policy rather than growing flags on the helper.

Prefer a small shared function with explicit parameters over a flag-heavy generic interpreter. If two abilities only look similar but differ in authored range source, target filtering, duration class, ownership, or cleanup, leave them separate until the common contract is demonstrated.

## Ownership and lifecycle

Procedures own their orders, validation, effects, state transitions, animation moves, timers, interruption,
inverse paths and cleanup. General unit and AI modules only resolve registry rows and send messages. Animation
think/end callbacks and save-registered thinker functions remain ordinary C callbacks where they represent
asynchronous lifecycle work rather than ability dispatch.

Runtime state stays on units and thinkers. Procedure identity must not be used as mutable state or as a cooldown
key: two rawcodes can intentionally share one procedure. `umove_t` stores an `abilityProc_t` only to identify the
behavior that owns a move.

`InitAbilities()` sends `A_INIT` once for each active registry row after AbilityData is loaded. The update roster
is built from `AB_UPDATE` rows and deduplicated by procedure, so shared behavior is called once per unit tick.
Registry indices are rebuilt from the registry; save/load persists IDs rather than pointers.

## Verification

The procedure conversion is covered by game tests for direct registry metadata, shared-procedure aliases,
Holy Bolt validation/execution, command dispatch, item charge consumption, autocast state, ability membership
and levels, persistent updates, animation ownership and save-safe rawcode identity. Run:

```sh
make build/lib/libgame-wc3-test.dylib
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_spell.*'
python3 tools/wc3_ability_class_audit.py --format=coverage
python3 -m unittest tests/test_check_wc3_ability_registry.py
make test
```
