# Corporeal and Ethereal Form separation ownership

`Acpf` and `Aetf` use the shared timed Morph transition, rather than Banish's
unit-target spell. Each direction retains its source type and existing ethereal
state through normal `Dur`, releases the counted separation contribution, then
rebinds the type and publishes the form's ethereal inverse.

The ethereal contribution is distinct from separation. Original `48b7f0` and
`48ec80` increment/decrement signed Unit+158. Corporeal Form acquires a source
when attached (`5e7140`); forward completion releases it (`5d3b00`) and reverse
completion acquires it (`5d3ac0`). Ethereal Form starts without a contribution;
forward completion acquires it (`5d3b30`), reverse completion releases it
(`5d3ae0`). Intrinsic `Aetl` contributes independently (`5e7290`/`5c5910`).

The game module retains a signed `ethereal_depth` and the initialized/held state
of each attached source on its optional ability owner. Type reconstruction sends
`A_UNIT_INIT` again, so that message must preserve an existing attachment rather
than acquire a second source. Removing one source preserves other sources and
timed Banish. The existing physical hit and attack-target policies consume the
combined ethereal state. Only first-positive and zero edges publish target
availability changes. Format201 saves the signed count and attachment ownership;
older formats are rejected.

Ghidra subtype detach functions `5c5660` (Corporeal) and `5c5930` (Ethereal)
release only their current ethereal form before delegating common Morph removal.
The shared timer, Stop retention, deferred scripted pause and saved requests
remain owned by the existing Morph implementation.

Evidence: [Audit333](retail-pathfinding-separation.md#audit333-corporealethereal-producer-evidence-integration-pending)
contains two matching894-event native streams and500 observer-free public
markers. Fresh Ethereal357 reports and Ghidra readback are archived at
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-01.2/ethereal357/`.
Fresh observe6 and observe9 reproduce all894 original native/marker rows exactly
under the existing presentation-timer exclusion; the fresh unhooked control3
reproduces all500 public markers. The public engine regression initially failed
322 of752 assertions; after fixture/schema and owner corrections, it passes
944 of944 in Classic and TFT, including automatic reverse after positive HeroDur.
The positive-lifetime regression first exposed16 failures in932 assertions: the
subtype completion bypassed the common lifetime scheduler. Delegating completion
to the existing GroundMorph owner preserves that timer and its saved inverse.
Two fresh lifetime repeats match all274 native rows and164 public markers,
including the unhooked control. The strict verifier passes16 tests and2,047
assertions per edition. Seventeen rejection tests protect the frozen evidence.

This integration does not close SEP-01.2's wider producer/replay requirement.
Submerge is covered separately by the Submerge360 transition work. Preplaced
alternate-form initialization, Phoenix and wider producer/replay coverage still
require explicit evidence before this leaf can close.
