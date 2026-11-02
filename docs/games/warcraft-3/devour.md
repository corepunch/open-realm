# Devour (`Adev`, `ACdv`)

## Contract and data

`Adev` (Kodo Beast) and `ACdv` (creep Devour, including Dragon Turtle) resolve
through the same `CAbilityDevour` in `skills/s_devour.c`. They must **not**
share Cannibalize (`Acan`); Cannibalize searches for corpses and heals the
caster, whereas Devour swallows a living unit and continues hurting it while
the caster can move and attack.

The requested alias is retained in the normal unit-target spell pipeline:

| Field | Meaning |
|---|---|
| `DataA` (`Dev1`) | Maximum level of a neutral-aggressive creep that may be swallowed |
| `DataB` (`Dev2`) | HP per second drained from the swallowed victim (stock 5) |
| `Rng` | Unit-target acquisition/cast range |
| `Cost`, `Cool` | Shared mana/cooldown handling |
| `targs` | Target-mask handling, combined with Devour-specific ground/non-Hero/organic/enemy checks |

Unlike a normal magic spell, Devour is physical and does not trigger Spell
Shield or rely on spell-immunity rejection. A second Devour is not allowed
while the caster already contains a living victim. Units that themselves have
`Adev` or `ACdv` are not eligible.

## Lifetime and ownership

1. Cast validation uses the normal ability `A_VALIDATE` and `A_EXECUTE` path.
   The caster/victim and requested ability alias are captured in a dedicated
   `S_SpellIdentityThinker` (`devour_think`), including both edict generations.
2. The victim retains its entity identity, player, life and mana, but is
   paused, hidden, nonselectable and removed from world interaction. Attacks
   and ordinary spell damage cannot hit it; movement, status update, natural
   regeneration and other simulation on it are suspended during digestion.
3. The thinker ticks at **1-second** intervals and deducts `Dev2` directly
   from life, without physical attack armor or retaliation. Caster movement,
   combat, and ordinary orders do not cancel an ongoing digestion.
4. On lethal digestion, normal unit death processing attributes the kill to
   the caster. The swallowed model stays hidden and has no raisable corpse.
5. On caster death (`unit_die`) or direct removal (`G_FreeEdict`), the still-
   living victim is released near the caster using the shared unstuck search,
   made visible/selectable, and unpaused at its **remaining** life.
6. Victim death/removal through another path retires the thinker without
   ejecting a living copy. Every cleanup is incarnation-checked, so an entity
   slot reused after a save or removal cannot become a new target.

The owner flag `AI_DEVOURING` and target flag `AI_DEVOURED` live in the
persisted `aiflags` word. The thinker stores damage rate in `wait`, next tick
in `freetime`, and its owner/target identity in the `channel_t` component.
`g_save.c` save version **123** rosters `devour_think` in the F_CFUNCTION
table, so the thinker is restorable in a mid-digestion save. No singleton or
external pointer map is used.

The existing **Cargo Hold** system is intentionally independent: Dragon
Turtle Devour is not an allied Load order or Meat Wagon corpse cargo.
`Advc` (Devour Cargo), which has separate capacity/eligibility behavior, is
not covered by this implementation.

## Regression checks

Automated `wc3_devour.*` fixtures exercise the real ability dispatch and
scheduler entry point, ordinary unit-target casting, a mid-digestion save/load
round-trip, different `Dev1`/`Dev2` values, outside-damage blocking,
ejection after caster death/removal, final digestion, and forced victim
removal. To run them on a developer build:

```sh
make test-wc3-engine WC3_PATTERN='wc3_devour.*'
make test
```

Manual retail comparison should check Dragon Turtle animation/cast timing,
exact swallowed-unit location on release at obstacles or bridges, spell/buff
presentation (`Bdig`), simultaneous script damage, and Kodo/creep AI cast
frequency. Morphing the caster during digestion (which retail may treat as
instant consumption) and extended `Advc` behavior remain separate follow-ups.
