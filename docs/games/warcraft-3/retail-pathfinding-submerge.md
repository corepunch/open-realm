# Submerge transition and separation ownership

Original `Asb1`, `Asb2` and `Asb3` retain their source form for the authored
transition duration (`.67`, `.7` and `.6` respectively). The shared Morph owner
acquires Unit+198 suppression before the deferred rebind and releases it at
completion. Stop preserves that transition; scripted pause defers its completion.
The game implementation now delegates Submerge to that same owner instead of
immediately replacing the unit type.

The Submerge subtype uses the common Burrow completion and reverse hooks.
Forward completion sets Unit+20 bit26, adds the invisibility contribution and
publishes the category inverse. Reverse admission removes invisibility and
republishes the normal category before acquiring transition suppression or
rebinding the source type. `4ba940` clears/restores category bits `c2` and retains
bit8; the occupancy word changes from `10110000` to `1000` while submerged.
The query word remains unchanged. Existing independent invisibility sources
remain effective when the Submerge source leaves.

A literal zero `Dur` still queues a transition. Public order return sees the old
type, no public order head and an acquired suppression contribution. The queued
callback completes afterward at the identical primary clock word. The timer
owner therefore preserves literal zero instead of forcing the minimum positive
delay. The generic GroundMorph public order head follows that zero-duration rule.

The production regression first failed134 of404 assertions per edition for
nonzero transitions; adding the zero-duration boundary exposed141 of427 combined
failures. Both tests now pass446 assertions in Classic and TFT. They cover both
AbilityData column schemas, non-stock endpoints/duration, Stop, pause, category
and query publication, saved pending/completed state and inverse transitions.
The complete unit suite passes147 tests and17,268 assertions per edition; combat,
ability dispatch, Channel, Avatar and ability lifecycle pass302 tests and20,455
assertions; save passes220 tests and67,951 assertions.

The portable Submerge360 verifier reconstructs both maps from the frozen input,
checks534 instruction guards from10 complete Ghidra functions, requires two
identical native captures and an unhooked control for each map, and runs focused
engine regressions. Its stock nine scenes contain749 public markers and1,331
native rows; the zero-duration scene contains83 public markers and137 native
rows. All identities, primary clock words, flags, category/query/configuration
and native rows compare literally after dropping observer sequence numbers and
the existing presentation-effect timer exclusion. Twelve rejection tests protect
the bundle. Isolated Save203 verification passes19 tests and1,592 assertions per edition.
The Ghidra project is saved; static export and readback match exactly.

Raw reports, maps, baseline binary/logs and fresh verification live under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-01.2/submerge360/`.
The earlier `submerge359` directory is diagnostic evidence only and covers two
families; it is not substituted for this three-family witness.

This covers explicit public Submerge transitions. Preplaced alternate-form
initialization, death during transition, authored positive form lifetime for
Submerge and the wider SEP-01.2 producer graph remain within the existing leaf.
Save203 records these transition semantics after the Captain202 layout.
