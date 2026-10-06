# Proposed text (not applied) — SEP-01.2

## TODO (`retail-pathfinding-todo.md`, SEP-01)
- SEP-01.2 evidence line: "Producers of the policy word are complete: 6f693d50 is the only gameplay producer (callers: unit init
  68a4f5, type rebind 670d4d, owner change 698f88, suspend events d0145/d0146, CancelAllOrders path, suppression acquire/release and
  channel-work begin/end tail-jumps); 1710e0 is otherwise called only by Mover_Destroy and load. Packed word
  `(param&15)<<16|(group&15)<<20|(((Unit+60&1)?15:owner)&15)<<24|(prio&15)<<28` (65,536 original setter cases). Live
  RS-SEP-01.2-policy (two identical observed runs + JASS control): aliases 17≡1 for param/group/prio, repulse=2 enables, selector 5–15
  inert (no movement, no cooldown, still a candidate, still draws on exact overlap), channel/pause/removal/owner-change timelines."
- Proposed new: **SEP-01.5** Live type-rebind (chaos/morph) and Unit_CancelAllOrders(5c.100000) refresh witnesses.
- Proposed new: **SEP-01.6** Identify the gameplay trigger of CAbilityMechanicalCritter slot+0x3e4 (only Unit+60 bit0 setter) and
  witness the owner-nibble-15 override; note UnitAddAbility('Amec') does not set it and the flag change does not refresh.

## Ledger (`retail-pathfinding-separation.md`, "Authored repulsion fields")
Replace "Unit+60 bit1 forces15" by "Unit+60 mask 0x1 (bit 0) forces 15"; add the producer table from HANDOFF §Functions and the
note that the selector setter's 8-bit store cannot leak into the category in practice because 1710e0 runs the category setter next.

## Corpus
Add `expected-SEP-01.2.json` (sha256 2d55fff8…de28d) and `oracle/policy-oracle.json` as research fixtures for SEP-01.2.
