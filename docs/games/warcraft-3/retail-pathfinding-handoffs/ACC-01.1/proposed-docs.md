# Proposed text (not applied) — ACC-01.1

## TODO evidence sentence (ACC-01.1)

Evidence: adaptive branch inventory (research ACC-01.1): all 289 conditional jumps (578 outcomes) of the
45 functions reached from 162cb0 annotated from assembly; 16 size-2/base predicate formulas confirmed by
127,792 original calls; 463 reachable outcomes, 115 unreachable with null/padding/context/entry proofs and 2
out-of-map-source outcomes owned by ROUTE-01.2. Accepted corpora already execute 458 outcomes; the ten
missing reachable outcomes (same-x setup, head reopen, five special-relax outcomes, special destination
blocked, E/S low-edge boundary predicates needing a marker) are witnessed by ACC-01.2.

## retail-pathfinding-search.md, after "Size-2 traversal and a reduced hierarchy witness" (new subsection)

### Adaptive branch preconditions

[Table B-IDs S1..RS from research ACC-01.1 HANDOFF.] Interior size-2 side predicates (163cd0, 163370 as
E/S interior, 1651a0) and the promoted-odd predicates (163d40, 1653e0) always accept during a search: the
cells they test lie inside the clear square that the lookup just returned. Only the level-boundary
predicates (163dd0, 1635b0, 164e50, 165470) and base/corner strips can reject. A corner opens only when both
adjacent side end segments were relaxed at a walker level above 0. Split halves use the tangent itself when
it is below the midpoint, otherwise mid-1 (level 0) or mid-stored-size (above) for the first half.

## Proposed new IDs

None.
