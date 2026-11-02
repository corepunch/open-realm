# Stone Form passive availability (SEP-01.2, payoff390)

Stone Form (`Astn`) uses the configured ordinary-Morph availability policy. Its
concrete primary table is `6fafeb3c` (COL `6fc138e0`, descriptor `6fd1b39c`,
`.?AVCAbilityStoneForm@@`). `+41c` resolves to the Misc policy reader `6f54c640`,
`+400`/`+404` to the common acquire `6f54eb80` and release `6f54cb50`. Stone
overrides removal `+a4` with `6f500230`, which calls common `6f540e30` and then
tail-calls its own `+c4` clock cancel `6f605d90`. It also overrides forward begin
`+408` (`6f50a670`), forward completion `+40c` (`6f50a590`) and reverse begin
`+414` (`6f50a540`). The inverse callback `+410` remains the one-byte `RET`
`6f4c8650`; the observer does not hook it.

The public producer uses real `war3mapMisc.txt` values 0 and 1, the stock
Gargoyle pair `ugar`/`ugrm`, the stock `.5` landing delay (`DataC1`), a `.375`
authored duration and a `.625` Apiv fade. Player 1 is an enemy without shared
vision/control in either direction; a positive pre-forward invisibility sample
prevents the allied-viewer ambiguity. Three ordinary scenes cover existing Apiv,
late Apiv and initial stone form. Two detach scenes remove Astn during the
pending landing or from the active stone form. Each setting/producer has two
observations and an observer-free control. The four maps rebuild byte for byte.

Configured availability follows the type-rebind phase `6f562c50`, not the
transition timer. That function is called from transition completion `6f551f70`
and from the immediate takeoff `6f56a000`. Its forward branch rebinds and enters
`6f552120`, which acquires; its reverse branch rebinds to the base while the
owner is still held, then `6f54ff50` runs `+414` followed by `+404`. The stock
forward order is a landing: the `ugar` source is retained for the `.875`
transition (Dur `.375` plus DataC `.5`), and acquisition follows the deferred
rebind at completion. The reverse order is a takeoff: it rebinds to `ugar`
immediately and releases at the order, before `6f569e20` acquires the `.375`
separation transition. Under setting one, every scene is visible to the enemy at
ages 35 through 41 and invisible from age 42 (`3.5 + .625`). A release at reverse
completion would delay that witness to age 45. Policy zero keeps the passive
published through both rebinds. Late attachment to `ugrm` is suspended at once;
initial `ugrm` attachment acquires synchronously through `6f55d380`.

Pending-landing detach forces the forward completion (rebind to `ugrm`, acquire,
`+40c`), then restores `ugar` through the reverse rebind and releases. It uses a
synchronous begin/end without a timer, as for Robo-Goblin and Chemical Rage.
Active detach restores the base the same way. Both leave Unit198 zero and a live
Apiv contribution: setting zero is fully published without fade handles; setting
one has live fade handles and no publication, and later samples witness the
restarted fade.

The port uses the reviewed configured helpers for initialization, completion and
detach in `CAbilityStoneForm`. The shared flight transition releases the owner
after a reverse takeoff rebind and before acquiring separation; a reverse landing
still releases at completion. Avenger Form never enters that branch. Existing
saved `morph_owned` and counted availability fields carry ownership; no
serialized field or format change is introduced. Stone's `+408` Unit `+15c`
target-availability owner (acquired at forward begin, released by `+40c`) and its
`+c4`/`+c8` clock are recorded literally in the captures but are not part of this
availability port. No claim is made about Avenger, Ethereal, Corporeal or Phoenix.

The genuine test-only baseline failed 166 of 2,982 assertions in each edition, all
of them setting-one cases. The failures are the configured owner itself (no
suspension at the landing rebind, initial attachment or late attachment) and its
inverse: no takeoff-time fade restart and no detach-time fade restart. Every
setting-zero assertion passed, as did configuration archive loading, both
rank-one data schemas, the flying/ground endpoints, `.875` landing and `.375`
takeoff timing, public order admission and in-process save/read. After the
port, the unchanged regressions pass 2,996 assertions per edition; the extra
fourteen come from delivered fade timers. The strict gate, including the
unchanged Chemical Rage380, Robo-Goblin375, ordinary Morph374, Submerge372,
Burrow366 and Morph331 regressions, passes 32 tests and 22,031 assertions per
edition. The wider check, which adds Morph382, Stone Form morph332/334 and the
`stoneform_*` regressions, passes 46 tests and 35,497 assertions per edition.
The preserved handoff reports 411 tests and 119,721 assertions for its
affected-suite check. Those counts cover the unit and save suites only:
its `wc3_spells.*` pattern matches zero tests. Integration uses the actual
`wc3_spell.*` suite and rejects empty runs; the corrected affected gate is
940 tests / 135,649 assertions per edition, including all 529 spell tests.
These saves call `WriteGame`/`ReadGame` in one process, using the same fixture
archive; they do not establish cold process restart or cross-map restoration.

`tools/ghidra/verify_wc3_pathing_morph390.py` validates 4,179 instruction guards,
29 data ranges, 12 captures, 10 scenes, 746 literal public markers and 1,788
projected events, including 1,034 native events and eight reference exception
rows. It checks the actual policy returns, landing/takeoff timers, acquisition at
the landing rebind, release after the takeoff rebind and before separation, the
public age-42 witness and both detach inverses. The 278 new Stone guards were
exported through read-only Ghidra GET requests by
`tools/ghidra/research/morph390_static_export.py` and are compared byte for byte
with the retail DLL. Only Unit `+128/+12c` links are normalized, using the
lifetime contract from Morph374/375. Original bytes and retirement ledgers are
pinned, with tamper coverage in `tests/test_wc3_pathfinding_morph390.py`.

Every observation reports two identical prestart system exceptions at
`0x7b55b6d9`, outside `game.dll`, before the first active marker; the handler
returns false and all public footers complete. These match the prior 375/380
signatures. Observer-free controls reproduce the full public streams but have no
exception observer, so they cannot establish the exceptions' cause or exclude an
instrumentation contribution. No exception-free or OS-fault-unreachability
claim is made. The first instrumented launch never reached the probe start
marker (24 loading inputs, no preload). It is retained as a diagnostic, outside
the accepted gameplay streams.

Local reports and original captures are under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-01.2/payoff390`:
`engine/red1-*` (failing-first logs, module and source), `engine/green1-*`,
`engine/strict-final.json` and `engine/affected-*`.
See [ordinary Morph](common-morph-passive-availability.md) for the shared policy,
retained-passive rebind and parser contracts, and [Stone Form](stone-form.md) for
the transition contract.

The composed recovery checkpoint (Stone390, Harvest012 and repaired Reach031)
passes9,974 native tests/34 reports and2,912 Python evidence tests, actual
`make test TEST_JOBS=16` exit0 under native SDL2. The composed unit/save/spell gate
passes941 tests/135,662 assertions per edition with the real `wc3_spell.*`
pattern. These gates preserve the remaining behavior limits above.
