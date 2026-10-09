# Temporary Captain enrollment: identity and initialization order

Payoff158 integrates automatic temporary enrollment for configured Town AI and
authored Captain homes. GROUP-03.4.6.2.1.2 remains open for its wider producer
and lifecycle requirements; the scope below is independently reproducible.

## Public producer and controls

The public producer creates four modified Footmen, starts campaign AI, creates
Captains and authors attack home (-1936,-144). It issues Move before applying
BTLF or BHwe and alternates the public `GroupTimedLife` policy. Two observed
repeats and an observer-free control reproduce all 182 Preload markers, including
current-order queries, health and positions at each 0.1-second sample.

Completed configured-home captures are under
`/GitHub/wc3-analysis/runtime/payoff158/`: `observe-6.jsonl`, `observe-7.jsonl`,
`control-3.jsonl` and their `*-preload.txt` files. The map is
`RS-Enrollment158c.w3m`, SHA256
`2581f603e2160e0f069fd4848bbbbd9f329cd86eff66ad03294a1518ec2af673`.
`tools/ghidra/fixtures/retail-captain-enrollment-policy-1.27.json` pins sources,
capture bytes, 182 public markers and 181 normalized semantic rows. The strict
verifier stops at the completion marker; post-completion idle calls are not part
of this bounded oracle. Archive: `runtime/captain-enrollment158/` under the
pathfinding report root.
Sources are `tools/frida/research/captain_enrollment158_*`; capture uses the
owned-process controller `captain_enrollment157_capture.py` in isolated B/C.

Earlier captures are retained separately. `observe-1` used the nonexistent
`SetGroupTimedLife` name, so the AI producer never established the intended
policy; it is a failed setup, not a grouping control. `observe-2/3` and
`control-1` establish the corresponding default-home producer, but do not prove
the engine's currently missing default-home construction.

## Identity, rather than only the public command value

`TownAI_AssignTemporaryUnitCaptain` (`9ccdb0`) invokes `6803f0(unit,1)` and
`693450` after a new attachment or a disabled-policy detachment. `693450`
constructs internal AI immediate command d0006 and appends it through `693490`.
The internal AI command can synchronously publish another Captain-owned Move.

| Application | Grouping policy | Retail result |
| --- | --- | --- |
| First/new attachment, ticks2/6 | Enabled | User and task identities change; final user count1 and public Move851986 |
| Same-Captain duplicate, ticks3/6 | Enabled | Both identity pairs and active task remain unchanged |
| Application, ticks10/11/16 | Disabled | Detach; empty user/task heads; user count0; public order0 |
| Reapplication after departure, tick20 | Enabled | New attachment and new user/task identities; public Move851986 |

Policy calls occur at ticks1/7/11/15/19 with values true/false/true/false/true.
The tick11 enabling call follows that tick's disabled application. The setter
does not sweep the existing roster. Each observed configured-home capture has
nine assignment calls, four attachments and three detachments.

Payoff157 observed only the unchanged numeric current-order value. It did not
establish preservation of the actual public head on first attachment. Preserving
that numeric field while retaining the old route would be an incorrect port.

The read-only observer also records `Unit+198`, which is the separation-disable
depth, not an inferred dispatch-depth field. During nested admission it reaches1;
`680320` then appends instead of canceling the current head. In the subsequent
internal AI transition, the resulting Captain Move becomes the new head.

## Initialization ordering and physical admission

The first configured-home private range call at tick2 returns raw word
`42f80000` (124.0 world units). It runs before the new timed-life buff becomes
visible to the BTLF lookup. Moving enrollment after publication would select the
temporary50 range and change the retained physical approach.

Only one direct `9d86f0` range call occurs in this scene. The complete
`CaptainAI_TestMemberReachability` (`9d8c70`) and bridge wrapper (`0594f0`)
explain the distinction: test Captain-to-unit first, then unit-to-Captain after
failure. These are adaptive distance queries with class0, work5000 and the
Captain-selected lane; a failed member-sized fine route is not equivalent.
The first attachment passes the first query and captures a private range. Later
new attachments fail both directions and receive point Move fallback. Both
repeats preserve 40 reissues, 79 reachability queries and 40 idle-member callbacks.

The bridge predicts the source; a unit target is read at its committed pose.
Fallback position retrieval also uses the committed pose. Unit exclusions are
cleared target, auxiliary source, predicted source, then restored in the same
order, retaining duplicate entries. The wrapper additionally excludes two
widget objects; the new engine helper covers the observed unit-object case,
not every possible additional static texture owner.

The positions are public R2S values with three decimal places. Their repeated
equality is a useful observer control; it is not a word-exact numerical engine
trajectory comparison. Exact mover/route replay, default-home construction,
guard inverse, retained-target/transport fallback, full idle reissue, specialized
buff composition and larger rosters remain required.

## Engine integration and verification

`S_ApplyTimedLife` calls the Town enrollment owner before allocating/publishing
the new record. Ordinary Town grouping defaults on; NeutralAggressive12 defaults
off. Starting a private AI VM retains pre-existing policy and Captain identity.
Enabled duplicates return before order cleanup; disabled applications withdraw
logical and physical ownership and Stop. New configured-home admissions use the
adaptive query and publish an internal Move without an extra public issued event.

Save132 writes logical roster encounter order, actor identity, authored home,
creation phase and Town grouping policy using entity indices. Backing storage and
VM pointers are never serialized. Physical Move membership remains separately
serialized and validated; logical array positions need not equal physical member
indices after prepared admission. Cold load restores the roster and policy after
`G_BotStop` has destroyed the process-owned arrays. Private AI script continuation
is still unavailable.

The actual CreateUnit/Move/timed-life regression initially fails eight of twenty
assertions. Cold logical roster restoration initially fails five of ten. A
predicting Captain separated from its committed pose exposes the incorrect
fallback source (10/11 assertions before correction). The production tests check
pre-buff range capture, task replacement, duplicate retention, policy-only
retention, disabled cleanup, disconnected point admission and issued-event count.

Focused Classic/TFT verification:

- Bot: 1,209 assertions across 94 tests per edition.
- Captain movement: 2,606,567 assertions across 23 tests per edition, including
  existing exact retail motion and saved-continuation fixtures.
- Save: 27,592 assertions across 193 tests per edition, including prior-version
  and incompatible-layout rejection.
- Read-only retail observer: two repeats and an observer-free control; five
  rejection tests exercise identity, reachability, detachment and control integrity.

The existing movement fixtures establish preservation of their previous retail
contracts. They do not establish complete trajectory parity for this new public
enrollment scene: its public positions are rounded R2S strings. No speedup or
full Captain fidelity claim follows from these results. Recruitment scans, guard
inverse, default Town homes and the remaining task scopes stay explicit.
