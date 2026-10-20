# Captain target clearing

This is an in-progress continuation of GROUP-03.4.7.3. The retail evidence is
complete for an ordinary point request and a single hostile-source follow. The production regressions failed seven behavior assertions out of50 in each
edition before implementation. The scoped engine inverse passes both production regressions, with50 assertions
in each edition. The separate selection owner remains under investigation.

`ClearCaptainTargets` enters `9d0f20`. It commits state 2, cancels the event-clock
control at Captain `+98`, then invokes `9d7760` for the additional identity at
`+120`, follow identity at `+114`, and resolved primary identity at `+108`, in that
order. The three direct return addresses are `9d0f59`, `9d0f7d`, and `9d0f8c`.
Each identity is resolved when its own step executes, so an earlier removal can
change what the next step receives. Null identities still call the handler,
which returns without performing a physical unit removal.

Two owned observations and an unhooked control of the point scene produce 233
identical public markers. At counter 1390, the retained request differs from
home, all target identities are null, and the complete observed Captain snapshot
is identical before and after Clear. The actor and member orders continue. The previous engine's unconditional `goal = home` write contradicted this boundary.

The source-follow scene has two observations and a control with 451 identical
public markers. At counter 1757, Clear removes a living hostile source from the
follow role, keeps the six-member roster and signed strength counters, changes
flags `1001` to `3001`, and restores the retained point through the ordinary
policy-1 movement refresh. Member target-dependent private orders are reissued
inside this callback. An unrelated Attack subscriber must remain attached to the
living source. A later combat alert can reacquire the same source within the
same simulation counter, after Clear has returned; that later event is separate
from Clear's synchronous inverse.

The fixture retains the entire observed synchronous callback stream, including
ordered member reissues. Process pointer identities are anchored in earlier
creation and roster records, rather than numbered afresh inside Clear; otherwise
permuting two member callbacks would disappear under pointer renaming. The
FloatMini vtable word in Captain `current[1]` is checked against the module base
plus `a9de14` and relocated explicitly. All other integer words remain literal.
Void methods' undefined EAX observations are excluded; the original callback
records remain in the immutable capture bundle.

`Captain368ClearEvidence.java` saves six complete function bodies as 551
instruction guards and appends the recovered name and evidence in Ghidra. The
saved readback is identical. `MapPathfinding.java` includes the corrected Clear
entry. `verify_wc3_pathing_captain368_clear.py` requires both repeated scenes,
unhooked controls, exact public completion, source hashes and the engine test
filter; fourteen Python regressions reject missing ownership, changed requests,
missing inverses and reordered role/member callbacks.

The first provisional C test used a Sorceress roster absent from the test MPQ.
Its AddAssault/count failures are fixture diagnostics, preserved in
`clear368-red-{classic,tft}.log`; they are not the accepted failing baseline.
The corrected production fixture passes all roster, outsider, ownership and save
setup checks, then fails exactly seven behavior assertions in both editions
(`clear368-red2-{classic,tft}.log`,43/50 across two tests). It uses the existing
Footman data with the same mana-only roster classification. The native Clear body has no unit-type branch.
The follow fixture also verifies save/load and an independent Attack subscriber.

## Separate event-clock owners

The older description of Captain Attack clearing a list at `+98` was inaccurate.
Assembly confirms calls to `EventClock_CancelRequest` (`0606c0`) at both Captain
Attack and Clear. The ordinary one-second `d01c1` periodic update uses a distinct
control at `+84`, visible in the constructor's `LEA ECX,[EBX+84]` at `9d3278`.

`9d7240` arms `+98` for repeating `d01c2`, using raw period `3dcccccd` (0.1) from
`cd53c4`. Its callers include `9cc850` and primary-target recovery `9d2700`.
Membership dispatch sends `d01c2` to `9d5e00`, a separate target-selection policy.
Static control ownership and cancellation are established; complete target-search
admission, candidate ranking, retries and transport policy remain within the
existing GROUP-03.4.7.3 task. New `captain369_*` public probes investigate this
owner rather than treating cancellation as the ordinary periodic update.

The scoped engine change resolves each current role after the preceding inverse,
sets state2, and invokes the specific Captain target callback. It preserves the
retained point and an unrelated Attack subscriber to the living source. It does
not synthesize physical unit destruction. The separately owned target-selection
request will be integrated with its own saved state under the same Captain leaf.

The strict combined Save208 run passes two tests and50 assertions in both Classic
and TFT (`verified-clear368-shared208.json`). It requires551 original instruction
guards, four observed captures, two unhooked controls and684 public markers.
The precise scoped inverse depends on the mixed target registry callback introduced
with Save208. This is a verified portion of the existing Captain leaf.
