# Retail point Attack admission

Payoff371 implements the public point Attack portion of existing `TARGET-01.2`.
It does not create another TODO or close the remaining producer inventory.

Retail `49a980` creates its `d016b` point task while `IssuePointOrder` is still
executing. The task retains the clicked world point and a zero authored range.
Move consumes it through `5ffb60` and `05b970`, using flag1 and the
`d0196`/`d0198` completion events. The common range conversion produces the
minimum `0x3efae148` fine-cell arrival range, without adding source collision.
This also happens when a Farm or a static blocked footprint occupies the goal.
Move owns the subsequent partial endpoint, retries and cannot-path result.

The engine previously created the Attack physical owner on a later AI tick and
replaced static blocked clicks with a nearby point during admission. The port
starts the existing point approach after installing Attack's walking state and
retains the raw waypoint. Target acquisition keeps its existing scheduler turn.
Saving before the first tick now has an actual physical owner to serialize;
the existing Attack completion callback owns it through restoration and Stop.

Two independent read-only Frida captures and an unhooked control cover each of
two maps. The eight-scene map covers empty and Farm-occupied Attack/Move,
integer-derived nearby float goals and zero distance. The four-scene map adds
a static blocked goal and adjacent public inputs at the actual quantized range
boundary. All 3,396 public markers agree, and both complete observed lifetimes
repeat exactly: 2,535 and 887 records. Every scalar, clock, heading, force
transition and completion remains in the fixture. Process pointers are renamed
only within each explicit unit lifetime; all aliases remain distinguishable.

The 672 arrival calls and 672 velocity commits in each independent repeat
match the production numerical helpers, covering 1,344 calls of each kind. Original x86 execution separately covers 19,196 reachable
integer square roots in the bounded output interval around `.49`: outputs
`0x3efae0f7` and `0x3efae1f7` straddle the fixed threshold, and no output equals
it. Public inputs `0x4403eb83` and `0x4403eb84` therefore test the actual inner
and outer decisions. Decimal distance alone would misclassify this boundary.

The failing-first shared baseline records 67/80 assertions per edition, with
exactly 13 missing immediate/restored owners or changed static goal coordinates.
All admission, public head, setup, unchanged pose, Stop and save checks pass.
After the port, both editions pass 29 focused tests and 11,306 assertions,
plus 30 queue/target tests and 2,679 assertions. The complete affected ferry
suite passes 123 tests and 9,547 assertions per edition. Its headless guard
fixture now requires a new point owner with a different creation sequence,
the Attack callback, the minimum arrival range and the retained guard point;
the failed target chase must have retired before that replacement.

The strict reproducer is
`tools/ghidra/verify_wc3_pathing_target371.py`; its frozen JSON/gzip fixture
contains the original 24-function, 3,216-instruction inventory and all six
accepted captures. Eleven rejection tests cover incomplete controls, missing
repeats, altered static clicks, late range publication and discarded heading
state. Ghidra notes are saved and their full readback matches the applied export.

Earlier controls that stopped loading input when initialization wrote its
startup file are excluded. That file predates the loading Continue gate; it
does not prove the timer has started. Accepted controls retain owned loading
Space input provenance and have matching completed public streams. The initial
survey's long decimal constants are also excluded from adjacent-boundary claims;
retail parsed different actual float words, which remain in the archived traces.

AttackGround is a separate producer: `49af00` selects weapon range and substitutes
`FLT_MAX` (`0x7f7fffff`) when initially in range. This payoff does not infer an
engine change for that owner or claim full trajectories, all ability producers,
or retail save-file parity.
