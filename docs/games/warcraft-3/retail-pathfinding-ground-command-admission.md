# Ground command admission and physical ownership

Retail `Attack_CreateAttackGroundTasks` (`49af00`) creates the selected weapon's
point approach during public admission. An initially reachable point supplies
`FLT_MAX`; a distant point supplies the selected range. Common Move normalizes
that range independently of source collision. The original `05b440` admission
predicate includes the predicted source position and source collision.

Open realm now starts that shared physical owner synchronously for translating
units. The Ground command keeps its existing public head and weapon delivery
policy. The physical owner retains the Ground adapter as its ability identity,
uses its completion callback, and survives save/load before any think runs.
Stop and replacement retire the physical owner through its leave callback.

The point validator `496410` rejects artillery/line points inside the configured
minimum range before task creation. It selects enabled artillery/line weapons
through `499530`/`494350`; ordinary weapons retain the accepted non-firing point
hold. Minimum eligibility uses the original `3f1740` near-equality comparison,
not the separate strict-below comparator used by other range producers.

Six independently repeated scene families retain matching unhooked public controls:

| Scene family | Observations / control | Public markers | Whole records |
| --- | --- | --- | --- |
| Footman/Mortar near and far | 2 / 1 | 1,134 | 927 |
| Exact and adjacent range/minimum boundaries | 2 / 1 | 1,698 | 779 |
| Stock `etrp` near and far, without physical Move requests | 2 / 1 | 570 | 204 |
| Blocked human/computer Ground, without an AI VM | 2 / 1 | 1,110 | 1,559 |
| Same blocked producer with complete internal/user queue observers | 2 / 1 | 1,110 | 1,815 |
| Public birth Stop, AI immediate and guard notification | 2 / 1 | 306 | 639 |

The boundary map authors exact float words through integer division. Footman's
last inside and first outside words distinguish `FLT_MAX` from range90.
Mortar's last inside and first outside minimum words distinguish rejection
`0x53` from accepted `FLT_MAX`. The complete moving captures compare all2,080 observed arrival/velocity pairs
with production arithmetic, with zero mismatches.

The stock `etrp` scene accepts both point orders but emits no physical Move
request: the near point retains the head, while the far point completes during
admission. Its actual building position snaps to544/1056. This observation does
not establish immobile artillery rejection rules. A first incomplete control
is explicitly excluded; its replacement reproduces both public streams.

The accepted pre-port engine baseline passes288/330 assertions in both editions:
42 failures expose absent immediate/cold physical owners and incorrect minimum
admission. All alive, translation, enabled-weapon, profile and authored-minimum
setup checks pass. The first port passes477/477 assertions on the same scenes;
the affected weapon-delivery fixture now visits the actual shared owner before
checking the original projectile geometry, damage and area masks.

A separate natural wall failure distinguishes a human source from a configured
computer before an AI VM starts. The two complete native observations retain
identical1,559-record compositions and match the control's370 public markers.
Human failure at counter1167 changes Move flags20 to16 and retires the physical
task while preserving the exact public Ground head. The computer at1767 changes
flags22 to18 through recovery, keeps its head during the failed transport query,
then invokes AI immediate and synchronously publishes Attack to guard272/304.
The guard replaces the public head before the failure callback returns. Birth
and later guard callbacks remain in the complete composition.

The corrected failing-first natural-wall regression passes66/80 assertions per
edition with explicit authored `Amov,Aatk` and passing ability setup checks.
All14 failures concern human head/goal retention, computer guard replacement or
the saved result. Setup, real cannot-path completion and cleanup checks pass.
The engine now recovers the mover while retaining the logical Ground head. A
saved failed owner keeps the human point dormant without creating another
approach; the computer tries the shared transport policy before AI guard return. The
shared AI dispatcher now retains a typed immediate user head, holds guard
admission under its internal scope and advances only after that scope returns.
No Stop order is fabricated for the human source.

The supplemental queue observations preserve the same public outcome while
recording the missing dispatcher boundary. `9bd6e0` acquires the same Unit198
counter used by common admission and creates internal owner `d0194`. Guard
Attack and its following immediate851974 append while this owner remains live.
The AI body pops that internal owner and releases198 before it returns. Only
then does outer `67df00` call `67de20`, retire the immediate user head and publish
Guard Attack before constructing its physical task. Neither counter release nor
internal pop independently activates user orders.

The public birth producer registers immediate and point observers before
`CreateUnit`. Ordinary Stop851972 precedes initial guard enrollment. Immediate
851974 and Guard851983 publish at depth0, with no physical task yet. The first
immediate callback relocates the newborn to997/936 while its enrolled guard
remains272/304. Both observations and the unhooked control retain all102 public
markers, including cleanup before the final sample/flush. All639 complete rows
match; caller scalars and read-only return-code evidence remain literal.
Earlier prefix99 captures and their three trailing markers remain diagnostics.

The independent idle AI regression records19 genuine failures per edition
(47/66), and public birth records20 (14/34). All setup checks pass. The typed
AI queue port preserves these event boundaries, private append versus
replacement caller roles, queued continuations and save/load. The six Ground
regressions pass778/778 assertions in each edition; the complete strict gate
passes43 tests/11,838 assertions per edition. A separate499/500-successor
regression verifies that private append shares the ordinary admission bound
and preserves the final typed packet through cold save. It is a boundary
protection test; no separate pre-port capacity failure is claimed.

The corrected intrinsic immobile baseline passes61/66 assertions per edition,
with five genuine head/goal failures. The narrow admission correction passes
66/66. Older weapon-delivery fixtures now visit the actual physical owner;
the stopped-pose fixture predicts the first stop commit before requiring exact
subsequent frozen samples. Damage, geometry, motion and resume checks remain.

The older explicit-weapon exemption fixture also visits the admitted Ground
owner after relocating its source. It publishes the fine position and runs the
real ability-owner update at primary clock8 before checking windup. The
existing deadline11, damage, save and exemption assertions remain unchanged;
no extra simulation time is inserted.

The portable fixture is
`tools/ghidra/fixtures/retail-target371-ground-1.27.json` and its compressed capture
bundle. `verify_wc3_pathing_target371_ground.py` checks2,757 original guards,
all5,923 whole records, provenance, matching controls and production arithmetic,
then runs fresh Classic/TFT regressions. Twenty-six integrity tests reject altered
range sources, missing minimum boundaries, truncated captures and invented
rooted physical requests, changed human heads and altered transport/guard
recovery. The two Ghidra helpers export36 complete functions and
save progress with matching readback. Supplemental queue/birth pointer aliases
apply only to explicit pointer fields and canonical source lifetimes. Unknown
source pointers, removed source identities, caller changes and altered
return-code operands fail verification.

The [point Attack admission analysis](retail-pathfinding-point-command-admission.md)
covers the separate range-zero producer. This advances the existing TARGET-01.2 leaf. Complete ground firing polls,
swing/projectile/stat numerics, other point producers and retail save-file
compatibility remain within their existing tasks. No new TODO child is added.
