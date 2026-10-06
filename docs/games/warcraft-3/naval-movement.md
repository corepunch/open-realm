# Naval Movement And Water Pathing

## Contract

Warcraft III movement comes from `UnitData.slk` `movetp`. Move compiles the
movement category with the runtime unit definition. Public retail getter and
publication captures establish the following masks:

| `movetp` | Blocking lane |
|---|---|
| `foot`, `horse`, `hover` | `0x02` |
| `fly` | `0x04` |
| `float` | `0x40` |
| `amph` | `0x80` |

The amphibious lane is independent mutable pathing state. The retail WPM loader
initially derives it when both walking and floating are blocked; later terrain
edits update the selected lane without recomputing it from the other two. The
router therefore tests the actual supplied byte mask. `0x80` cannot double as a
require-all query modifier. See [authored movement masks](retail-pathfinding-engine.md#authored-movement-masks-reach-terrain-and-object-queries)
and the BASE-02 terrain-mutation evidence in that ledger.

The upstream water summed-area table is retained: static floating-radius queries
use four prefix reads instead of rescanning every covered cell. Native lane
semantics, fine/adaptive searches and saved routing state remain unchanged.

## Pathing Textures And Occupancy

Original widget blue coverage publishes category `0xc2`, blocking walking,
floating and amphibious queries. In the decoded `color32_t` representation this
coverage is `COLOR32.b`; green-source coverage retains the flight lane. A live
walkable bridge may clear walking on its authored deck while retaining underlying
floating pathing. Its blocked pixels retain the three ground occupancy lanes.

The encountered unit's query mask is separate from its occupancy category.
Captured Footman, horse, hover, float and amphibious units publish category
`0xca`; flyers publish zero. Command-time occupancy consequently blocks the three
ground query lanes even when the encountered unit has a different movement type.
Move-time circle collision retains its existing air/non-air separation and precise
`BoxEdicts` broad phase. The upstream ground/sea domain approximation is not used
because it contradicts these category publications.

## Height And Consumers

Support height is independent of pathability. Existing `M_CheckGround()` keeps
FLOAT on water rather than bridge height and selects amphibious water height from
the terrain/support state. Render-water detection does not authorize movement.

Move, Patrol, Attack/chase, formation slots, spawn placement, Way Gates, Blink,
construction approaches and cargo routing consume `M_UnitStaticPathingFlags()`.
Repair's naval range bonus remains owned by Repair. Cargo's existing last-resort
unload placement remains unresolved retail work.

## Verification And Limits

Asset-free tests cover all four independent mask bits, authored movement-type
selection, real Move detours, widget footprint publication/release and command
occupancy. The upstream connected-water detour test is retained. Bridge/altitude
tests exercise height separately. Public retail captures and the original fine
search oracle establish these mask/category contracts; complete boat/amphibious
trajectories and support transitions remain BASE-02.1 work in the retail backlog.
