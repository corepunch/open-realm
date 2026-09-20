# Unsummon Building

## Contract

`Auns` is ROC/TFT `CAbilityUnsummon` (parent `AAsm`). No `AbilityData` aliases
share `code=Auns` in stock data.

| Field | Stock | OE label | Runtime |
| --- | ---: | --- | --- |
| `DataA` | 0.5 | `recoveredResources` | maximum fraction of building gold/lumber cost recoverable by Unsummon |
| `DataB` | 50 | `acumulationStep` | demolition damage per second |
| `targs` | ROC `structure,debris,player`; TFT `structure,player` | — | owned living structure |
| `Cost` / `Cool` / `Rng` | 0 / 0 / 0 | — | free, no cooldown; `Rng=0` does not bypass worker interaction range |

Retail/Classic documentation describes Unsummon as destroying the building over
a short period, returning 50% of its resources when the demolition itself
removes all of the building's life. It also documents 50 damage/second and
notes that enemy damage during Unsummon reduces the returned resources.
Buildings are spell immune while Unsummon is active.

OpenRealm therefore treats `DataB` as demolition HP/second and earns the
`DataA` resource pool in proportion to HP actually removed by Unsummon:

```text
recoverable gold   = UnitBalance.goldCost   * DataA
recoverable lumber = UnitBalance.lumberCost * DataA

per tick:
  removed = min(current HP, DataB * simulation_dt)
  refund += recoverable * (removed / max HP)
```

Fractional gold/lumber is accumulated on the ability thinker and only whole
resources are credited. Damage from attacks/spells is not part of `removed`, so
it naturally reduces the maximum final refund.

## Lifecycle

`Auns` is a normal unit-target spell plus `AB_CHANNEL`:

```text
cast accepted
  -> Acolyte approaches the building's pathing footprint
  -> once the worker reaches collision-sized interaction range, start channel
  -> apply Buns status to target
  -> target becomes spell immune
  -> ability-owned thinker ticks every simulation frame
     -> subtract DataB * dt HP directly (armor-independent demolition)
     -> credit proportional DataA refund
  -> target reaches 0 HP
     -> ordinary unit_die
     -> remove Buns
     -> end channel
```

Movement, a replacement order, caster death, or other ordinary channel
cancellation stops future demolition. HP already removed and resources already
returned remain; the target's `Buns` immunity is removed immediately by the
ability's `A_CANCEL` path.

The approach phase is owned by the Unsummon ability and reuses the worker
movement contract used by Repair and construction interactions. For buildings
with authored pathing, the distance is measured from the Acolyte to the
building footprint; otherwise the worker and building collision radii are used
as the explicit fallback. `Rng=0` is never treated as infinite spell range.
Issuing another order, stopping, moving away after the channel starts, an
unreachable approach, or caster death cancels the pending/active operation
without undoing already-earned demolition or refund.

The active thinker owns the demolition state (`owner`, `goalentity`, target
generation, level, fractional refund accumulators); the caster retains the
pending target and approach state until the channel starts. `unsummon_think`
is appended to the save callback roster so a save taken mid-Unsummon resumes
the live channel rather than serializing a process address.

## Temporary spell immunity

TFT class data identifies `Buns` as `CBuffUnsummon` even though stock
`AbilityData` does not supply a `BuffID` for `Auns`. OpenRealm installs the
status explicitly for the active demolition and includes it in
`S_UnitSpellImmune()`. Physical attacks remain possible; that is required for
the documented reduced-refund behavior when enemies damage a building during
Unsummon.

## Cost source

The base cost is read from `building->data.UnitBalance` when present, otherwise
`G_UnitBalance(class_id)`. Do not hardcode stock building prices.

Retail patch 1.17 changed upgraded buildings so Unsummon also recovers the
original building's cost in addition to upgrade cost. OpenRealm currently does
not retain a completed building's full upgrade-cost ancestry after the in-place
type transform, so exact upgraded-town-hall refund parity remains separate work.

## Remaining fidelity gaps

- Exact Acolyte work animation / Unsummon target presentation is not yet
  modelled beyond the authored target effect and `Buns` state.
- ROC's `debris` target token is not implemented as a separate target class.
- Patch-1.17 upgraded-building accumulated-cost recovery needs persistent
  upgrade ancestry/cost accounting; current refunds use the completed unit row.

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.unsummon*'
make test-wc3-engine WC3_PATTERN='wc3_save.unsummon*'
```

Focused tests use non-stock `DataA=.25`, `DataB=80`, and `Cost=15` so a stock
constant cannot pass. They cover approach-before-start, start-at-range,
progressive demolition/refund, enemy damage reducing the result, interruption
before and after channel start, invalid target rejection, and live-thinker
save/load.
