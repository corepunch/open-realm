# Hero revival

OpenRealm models Altar-style Hero revival as production of an existing Hero
entity, not as training a replacement unit.

## Capability

Revival capability is data-driven from the unit profile `Revive` (`urev`)
field. Any building whose normalized profile has a non-zero `Revive` value can
offer eligible dead Heroes owned by the same player. No Altar rawcode is
special-cased.

## Hero death state

Heroes keep their authoritative edict after death. When the death animation
finishes, `DissipateTime` is used for the Hero corpse/dissipation timer. At the
end of that timer the Hero is hidden and marked `revival.awaiting` instead of
being freed. `EVENT_PLAYER_HERO_REVIVABLE` and `EVENT_UNIT_HERO_REVIVABLE` are
published at that transition.

A dead Hero remains counted by `G_GetPlayerTechCountValue()`. This preserves
Hero/techtree limits while the Hero is dead.

## Command card and identity

A `Revive=true` building enumerates eligible dead Hero *instances*. Each button
uses the Hero unit's art and carries a `revive:<entity-number>` server command,
so multiple Heroes of the same type remain distinct. Queueing one Hero sets its
`reviving` flag immediately, which removes it from every other eligible
building's command card.

## Cost and time

The implementation reads the authoritative Misc gameplay constants:

```text
ReviveBaseFactor
ReviveLevelFactor
ReviveBaseLumberFactor
ReviveLumberLevelFactor
ReviveMaxFactor
ReviveTimeFactor
ReviveMaxTimeFactor
```

Gold uses the Hero's `goldCost`; lumber independently uses `lumberCost`. Cost
factors grow by Hero level and are capped by `ReviveMaxFactor`. Revival time is
the Hero's normal `buildTime * level * ReviveTimeFactor`, capped by
`ReviveMaxTimeFactor`.

Gold and lumber are charged when the Hero is inserted into the producer's
existing linked production queue. The Hero itself is the queue identity; a
separate `revival.queue_next` pointer avoids reusing its normal `build` field.

## Completion and cancellation

Only the queue head progresses. On completion the normal deterministic producer
exit search (`SP_FindUnitExitPosition`) finds a legal point beside the Altar,
then `G_ReviveHero()` restores the same Hero object. The scripted revive helper
also clears hidden/revival state and applies both mana terms:

```text
max mana * HeroReviveManaFactor
+ initial mana * HeroReviveManaStart
```

The finish event is published after the Hero is alive and positioned. An active
revival exposes the existing command-card Cancel action; cancellation refunds
the exact gold/lumber charged, clears `reviving`, and leaves the Hero awaiting
revival. Destroying a producer cancels/refunds all Hero revivals in its mixed
production chain while preserving unrelated queue links.

## Remaining gaps

- The generic unit-training queue does not yet reserve/release unit food, so
  Hero revival deliberately does not invent a Hero-only food accounting path.
  Food reservation should be added once to the shared production queue.
- Exact proper-Hero-name formatting and a Hero-level number overlay are not yet
  represented by the current command-button wire format.
- Revival tooltip resource icons/cost fields are not represented separately by
  `gameCommandButton_t`; authoritative charging is implemented server-side.
- Rally orders after revival are not yet applied.
- Tavern/instant Hero awakening remains a separate future mechanic.
