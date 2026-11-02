# Chemical Rage passive availability (SEP-01.2, payoff380)

Chemical Rage (`ANcr`) uses the configured ordinary-Morph availability policy.
Its concrete primary table is `6fae0b24`: `+41c` resolves to the Misc policy
reader `6f54c640`, `+a4` to detach `6f540e30`, and `+40c` to the concrete forward
callback `6f4c84c0`. The inverse callback `6f4c8650` is a single `RET`; guarding
that byte is safe, but attaching the initial Frida interceptor there failed.
The corrected observer omits that hook. Both observer sources and all four
failed original launches remain in the evidence inventory.

The public producer uses real `war3mapMisc.txt` values 0 and 1, `ANcr`, `Nalc`
and `Nalm`, a `.375` transition, `2.5` alternate lifetime and `.625` Apiv fade.
Player 1 is an enemy without shared vision/control in either direction; a
positive pre-forward invisibility sample prevents the allied-viewer ambiguity.
Three ordinary scenes cover existing Apiv, late Apiv and initial alternate form.
Two detach scenes remove ANcr during its forward transition or active alternate
form. Each setting/producer has two observations and an observer-free control.
The four maps rebuild byte for byte.

The native producer acquires configured suspension at transition completion,
holds it until natural reverse completion or detach, and then restarts Apiv's
authored fade. Policy zero completes the retained passive at the type rebind.
Initial alternate attachment also schedules the authored alternate lifetime:
`6f55d380` reaches `6f552120`, which invokes the concrete forward callback.
Pending-forward detach first forces that same completion (including the lifetime
timer request) before immediately restoring the base type. The age50 marker in
the first two ordinary scenes precedes fade completion; the later positive
samples at ticks 51 and 121 establish that completion instead.

The port shares the reviewed ANrg375 availability helpers and changes only
`ANcr` dispatch in `s_requested_abilities.c`. Its initial-alternate lifetime
helper uses the existing morph fields and authored duration. It makes no claim
about Metamorphosis (`AEme`) or other subclasses. Existing common-Morph removal
already produces forced completion followed by inverse and required no change.

The genuine test-only baseline on exact Save214 tree
`8893da3345a6febf8f14275aa58eec23aa871e75` failed 250 behavior assertions in each
edition: ordinary 2046/2268 and detach 900/928. Configuration archive loading,
both rank-one data schemas, public order admission, positive invisibility,
save/read, and pending-detach completion/timer/inverse setup all passed. The
unchanged regressions pass 2338 ordinary and 936 detach assertions per edition
after the private port. The final strict gate, including the unchanged ANrg375,
ordinary Morph374, Submerge372 and Burrow366 regressions, passes 30 tests and
19026 assertions per edition. The affected unit, spell and save suites pass 922 tests
and 116953 assertions per edition. These saves call `WriteGame`/`ReadGame` in
one process, using the same fixture archive; they do not establish cold process
restart or cross-map restoration.

`tools/ghidra/verify_wc3_pathing_morph380.py` validates 3901 instruction guards,
16 data ranges, 12 captures, 10 scenes, 746 literal public markers and 1652
projected events, including 898 native events and eight reference exception
rows. Whole concrete functions were exported, saved and read back identically.
Only Unit `+128/+12c` links are normalized, using the proven dereference,
reference-count release and last-observed-holder-clear lifetime contract from
Morph374/375. Concurrent aliasing remains exact; every other scalar, identity,
clock, public marker and exception field remains literal. Original bytes and
retirement ledgers are pinned, with tamper coverage in
`tests/test_wc3_pathfinding_morph380.py`.

Every observation reports two identical prestart system exceptions at
`0x7b55b6d9`, outside `game.dll`, before the first active marker; the handler
returns false and all public footers complete. These match prior375 signatures.
Observer-free controls reproduce the full public streams but have no exception
observer, so they cannot establish the exceptions' cause or exclude an
instrumentation contribution. No exception-free or OS-fault-unreachability
claim is made. The original failed captures remain diagnostic, outside the
accepted gameplay streams.

Local reports and original captures are under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-01.2/payoff380`:
`isolated214-failing-first.json`, `isolated214-policy-green.json`,
`isolated214-affected.json`, and `verified-private214-final.json`.
See [ordinary Morph](common-morph-passive-availability.md) for the shared policy,
retained-passive rebind and parser contracts. Registration and acceptance of
this private change are separate from the completed native-only evidence.
