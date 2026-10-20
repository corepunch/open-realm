# BASE-02.2 proposed ledger/TODO text (not applied)

## TODO (`retail-pathfinding-todo.md`, BASE-02.2 line) — proposed evidence sentence
Object categories: mover fine objects (15ed40 active 01000000; units by movetp via 685e30 → ca/0/08, items 18,
CaptainAI 02→table&~8, runtime ability edits) and static region objects (14cf70 +40 10000000: widget pathTex
regions c2/10/08(+04), inactive CTriggerRegion). Live Footman 010000ca, item 01000018, LTcr c2/10/08. Consumers
read no player ownership. Evidence: research/BASE-02.2, FOOT-03.1/03.2 oracles and capture. Remaining: live
building/under-construction/gold-mine/Way-Gate rows (E2E-02.2), missile/destructable own-mover activity (new BASE-02.8).

## New ID (text only)
- **BASE-02.8** Establish whether missile movers and destructable own movers ever publish a nonzero fine category or
  active occupancy, and whether items can carry pathing textures (CItem vt+b0 constant path); live capture each.

## `retail-pathfinding-search.md` — replace "Exact object categories remain unresolved" with
Fine-map objects are CPmRegion records (`14cf20`). Movers publish `+34 = 01000000 | category` (unit movetp table,
item `18`, CaptainAI `02`); widget pathing textures create static (`+40 10000000`) region objects with categories
`c2/10/08` (+`04`). Only static objects are hierarchy-eligible (FOOT-03.1).

## `retail-pathfinding-types-1.27.json` — WC3SpatialPrefix field additions (proposal)
`+0x14 identity (registry slot/generation)`, `+0x1c rectangle (4×i32, -1 empty)`, `+0x2c map`, `+0x30 payload`,
`+0x34 category_flags (bit24 active, low24 category)`, `+0x38 visit_stamp (-1 retired)`, `+0x3c record_count (low24)`,
`+0x40 occupancy_flags (low28 suppression, 10000000 static, 20000000 moving, 40000000 group, 80000000 excluded)`.
