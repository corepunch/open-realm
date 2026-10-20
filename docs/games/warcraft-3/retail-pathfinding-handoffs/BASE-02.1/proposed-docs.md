# BASE-02.1 proposed documentation (not applied; owner applies serially)

## Proposed TODO text (shared TODO is not edited by research)

- BASE-02.1 stays open. Suggested evidence note once the owner has failing/passing regressions:
  "Authored producer `movetp` -> `685340` (case-insensitive Storm `SStrCmpI` exact match over 11 names;
  anything else, including combined/whitespace strings, is 0) -> row+1a8; `685e30` query/category and
  `685db0` movement class; 60 original parser cases, 810 table calls, 60 builder-slice cases and two
  identical live captures with 15 custom `umvt` clones, crow form (Amrf/Arav), burrow and Ensnare."
- Proposed new IDs (text only):
  - **BASE-02.8** Runtime profile producers beyond SetUnitPathing: forced-ground push/pop (Ensnare/Web/
    Roots), morph landing/takeoff interim state, burrow/submerge category toggles, harvest/wind-walk
    ghost profile (query `|10`, category `&29`), Locust/LandMine/TornadoWander/WispHarvest fixed
    profiles. Live evidence exists for Ensnare, crow form and burrow only; the rest are instruction-level.
  - **BASE-02.9** Fly-height presentation and support Z per movement type (`66d780`/`68f390`):
    `Unit+20` bit 800 (set by morph-ability add when either form flies, never cleared by removal),
    hover authored height, flyer blend with layer -3, deep-water amphibious rule. Water/bridge/cliff
    rows are owned by MAP-02.2.

## Proposed ledger section (retail-pathfinding-engine.md, after "Authored movement masks ...")

### Authored movement-type producer and runtime lane inventory (BASE-02.1 research)

`UnitProfile_BuildFromUnitData` (`66bf40`) reads the UnitData `movetp` column (`6b3640`, SLK +e0)
for every profile row at map load (852 rows live, including custom w3u `umvt` overrides, byte-exact)
and calls `UnitMovementType_ParseName` (`685340`). The parser is a linear, case-insensitive exact match
(Storm ordinal 509 `SStrCmpI` -> msvcr120 `_strnicmp`, default locale) over the table at `6fce6000`:
`fly`2, `hover`8, `foot`1, `horse`4, `unbuild`40, `float`10, `amph`20, `_`/`-`/`none`/empty 0.
Any other text, including `foot,fly`, ` foot`, `foot ` and `amphibious`, yields 0. A NUL ends the string.

| movetp | bits | query (+1b0) | category (+1ac) | class (685db0>>1) | adaptive lane mask | fly offset (e8) |
| --- | --- | --- | --- | --- | --- | --- |
| foot, horse | 1, 4 | 02 | ca | 0 | 06000006 | 0 (or Unit+214 if Unit+20&800) |
| hover | 8 | 02 | ca | 0 | 06000006 | authored moveHeight (+1f8) |
| fly | 2 | 04 | 00 | 3 | 04000004 | interpolated Unit+214 (moveHeight max, moveFloor min) |
| float | 10 | 40 | ca | 2 | 40000040 | 0 |
| amph | 20 | 80 | ca | 1 | 80000080 | 0 |
| unbuild | 40 | 00 | 08 | 0 | 06000006 | 0 |
| none, _, -, empty, invalid | 0 | 00 | 00 | 0 | 06000006 | 0 |

Unit birth (`68a060`) copies row+1a8 to `Unit+1fc`, publishes the class through `05c6f0`, enables
adaptive routing for every mobile type, sets `Unit+5c` bit 20000000 only for bits==2 and publishes
query/category twice through `6945a0`. Type rebind (`670950`) repeats this for the new row and
disables adaptive routing for flyers. Runtime producers that change query/category without a class
publish: forced-ground push (`6877b0`, first push with authored air target type: ca/02, `Unit+200`++)
and pop (`69c840`); morph landing (`569b80`: `Unit+1fc`=1, ca/02, adaptive on, before the rebind);
burrow (`4ba8d0`: category ±2/±8, query retained). Live Ensnare keeps the flyer's `Unit+1fc`=2 and
movement class 3 while its fine query is 02; morph landing keeps class 3 and `Unit+5c` bit 20000000
until the delayed rebind.

Support Z is `CUnit_GetSupportZ` (`66d780`, vtable e4): flyers (`Unit+5c`&20000000, not forced
ground) blend terrain/deck height and the layer -3 height by current fly height / max; other units use
terrain/deck height (`78d1e0`), raise to the water surface for hover/float/amph (`Unit+1fc`&38;
amphibious only where `PathWorld_IsDeepWaterPoint` held at the last `684480` refresh), then add the
`68f390` offset. `GetUnitFlyHeight` returns `Unit+208` directly, so a ground unit reports a scripted
height that does not move its support Z unless `Unit+20` bit 800 is set (Amrf add/remove).

## Proposed corpus entries (not applied)

- original-code oracle `verify_base021_movement_types.py` (game.dll + Storm.dll + msvcr120.dll hashes),
  frozen `expected-BASE-02.1.json` sha256 recorded in HANDOFF.
- live contract `types2-first` / `types2-repeat` with `verify_base021_types_trace.py --compare`.
