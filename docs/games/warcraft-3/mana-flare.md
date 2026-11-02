# Mana Flare

## Contract

`Amfl` is TFT-only `CAbilityManaFlare` (parent `AAsm`). ROC `AbilityData`
has no `Amfl` row. It is **not** FrostNova (`AHfn` / `CAbilityFrostNova`);
ignore `ability_map.c` and the old TODO comment.

No-target toggle spell (`AB_SPELL | AB_TOGGLE | AB_UPDATE`, `SPELL_TARGET_NONE`).
Activation preflights the first `BuffID` token (`Bmfl` in stock data) against
status capacity before any mana or cooldown commit. A full 16-slot status list
rejects activation without spending resources, while an already active Mana
Flare can still be toggled off at capacity. Successful activation spends Cost
and applies `Bmfl` for `Dur`/`HeroDur` seconds. While `Bmfl` is active, the Faerie Dragon is immobile and every command
button except Mana Flare is disabled. Movement and other orders are rejected
without removing the status; clicking Mana Flare again ends it early. Natural
status expiry restores movement and commands. The shared no-target cast path
recognizes a running Mana Flare before resource/cooldown validation: turning it
off runs only the status-removal callback, without a second spell commit, cast
event, mana cost, or cooldown reset. Explicit immediate orders
`manaflareon` (852512) and `manaflareoff` (852513) are idempotent: an already
active on-order and an already inactive off-order do not change resources.
Activation by `manaflareon` still uses the normal cast checks and commit, and
both orders require the unit to possess `Amfl`. `IssueImmediateOrderById`
resolves the same two names through `G_OrderId2String`. The immediate-order
resolver must pass the resolved `Amfl` ability code to `S_AbilityMessage`:
an order payload with code zero would otherwise be rejected by the active
Mana Flare command lock before `A_ORDER` could switch it off.
`Bmfa` (`CBuffManaFlareAoe`,
parent `BAOE`) is presentation-only and is not required for damage or armor.
Phase Shift (`Apsh`) is rejected during the active status without spending mana.

While `Bmfl` is active, nearby enemy units that successfully cast a spell
take damage proportional to that spell's mana cost. Friendly casts and
out-of-`Area` enemies are ignored.

| Field | Stock | AbilityMetaData | Runtime |
| --- | ---: | --- | --- |
| `DataA` | 3 | Unit - Damage Per Mana Point | unit damage = `min(DataC, cost * DataA)` |
| `DataB` | 1 | Hero - Damage Per Mana Point | hero damage = `min(DataD, cost * DataB)` |
| `DataC` | 90 | Unit - Maximum Damage | unit damage cap |
| `DataD` | 50 | Hero - Maximum Damage | hero damage cap |
| `DataE` | 12 | labeled "Damage Cooldown" | **armor bonus** while `Bmfl` is active (`Ubertip` binds `<Amfl,DataE1>`) |
| `DataF` | 1 | Caster Only Splash | non-zero → splash only hits units with a mana pool |
| `Area` | 750 | — | detection radius around the Faerie Dragon |
| `Rng` | 200 | — | splash radius around the primary victim |
| `Cast` | 0.75 | — | seconds between flares from one `Amfl` unit |
| `Dur`/`HeroDur` | 30 | — | `Bmfl` status lifetime |
| `targs` | air,ground,enemy | — | flare victim filter (not the cast target) |
| `BuffID` | `Bmfl,Bmfa` | — | `Bmfl` on caster; `Bmfa` unused in gameplay |

`DataE` AbilityMetaData display name conflicts with the ubertip armor
binding. Stock `DataE=12` matches the documented armor bonus, and stock
`Cast=0.75` matches the documented flare interval, so runtime treats
`DataE` as armor and `Cast` as the fire cooldown.

## Data Flow

```text
AbilityData.slk (Amfl)
  -> Cost, Cool, Area, Rng, Cast, Dur, DataA-F, BuffID, targs
CAbilityManaFlare
  -> S_CastNoTargetSpell / A_EXECUTE: attributed timed Bmfl status
  -> toggle off / disable: strip Bmfl
  -> A_UPDATE: maintain channel animation while Bmfl exists
spell_commit (successful cast)
  -> S_ManaFlareOnCast(caster, code, level)
     scan enemies-of-caster with Bmfl status in Area
     throttle per Bmfl instance using status.next_tick (first cast at time zero works)
     damage primary via S_SpellDamage; optional Rng splash
G_UnitArmorValue
  -> S_ManaFlareArmorBonus (DataE while Bmfl)
```

## Registry

```c
{ "Amfl", CAbilityManaFlare, AB_SPELL | AB_TOGGLE | AB_UPDATE | AB_STATUS_EVENTS, SPELL_TARGET_NONE },
```

## Persistence and remaining parity

The per-status flare interval deadline uses `heroabilitystatus_t.next_tick`.
`data` remains reserved for the status payload/legacy origin rawcode; using it
for cast timestamps could dispatch an unrelated status ability. `Bmfl` is explicitly
attributed to `Amfl` for status lifecycle dispatch.

`Apsh` currently delegates its non-channel behavior to the existing Divine
Shield implementation; true Phase Shift mechanics and any Mana Flare missile
visual effects are separate follow-up work.

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Amfl
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.mana_flare*'
```

Focused tests cover procedure registration, authored non-stock DataA damage,
enemy-cast trigger, out-of-area ignore, friendly-cast ignore, armor bonus,
Cast interval gating, toggle/expiry cleanup, order lockout and button state,
splash mana-pool filter, time-zero cooldown, status retaliation without a spell
channel, and Phase Shift rejection without mana loss.

The full-status-slot regression uses non-stock `Cool=5` to confirm rejection
leaves both mana and cooldown unchanged; freeing one slot permits activation.
The toggle-off regression fills the other 15 slots after activation and
confirms the original status can still be removed. Additional regressions
confirm direct deactivation with zero mana and a running cooldown, stable
cooldown and mana on repeated on/off orders, rejection of on-orders during
cooldown and when the unit has no Mana Flare ability, and the Warcraft order
IDs for the string and `ById` JASS paths.
