# Captain target selection

This research continues the existing GROUP-03.4.7.3 leaf. The Save209 source port now queues ordinary selection independently from
periodic updates, evaluates one eligible candidate each 0.1 seconds, and persists
pending identities and ranking buckets. Its strict verification passes both editions (3 tests,72 assertions each);
the full save suite also passes224 tests and68,044 assertions per edition.
The older Suicide behavior remains under investigation. No new
TODO children are required.

The native registrations bind `CaptainVsPlayer` to `9b8b90`, `CaptainVsUnits` to
`9b8c20`, `SuicidePlayer` to `9c02d0`, and `SuicidePlayerUnits` to `9c0360`.
They reach `9cc850` with different raw flag80/flag4 inputs. These flags do not
mean that VsUnits filters out structures. With raw4 clear, selector17 complements
the Captain owner's outgoing passive-alliance mask; raw4 set uses selector24 to
restrict enumeration to the requested player. The direction is own-to-candidate
owner, independently of the reverse alliance. The admission wrapper rejects an absent
Captain and a requested full-house check when flags.bit1 is clear; the current
engine's additional empty-roster and retreat rejection are unsupported here.

`9d7240` writes requested player `+130`, resets twelve weak identity/score
buckets, enumerates candidates through `9cf8a0`, and arms repeating `d01c2` on
control `+98` with period word `3dcccccd` (0.1 seconds). The ordinary one-second
`d01c1` update uses a separate control at `+84`. The pending intrusive list has
head `+138` and tail `+13c`; the consumer pops the oldest candidate. The native
collector retains strong Unit references. A tick skips invalid, inactive,
script-invulnerable, hidden or undetected entries without spending its eligible budget,
then processes one eligible candidate. If candidates remain, selection continues
on the next tick. Clear and CaptainAttack cancel the selection control. Script invulnerability is
the widget20.bit8 producer represented by `unit->invulnerable`; the broader
engine damage-immunity predicate also includes UnitE8 and must not replace it.
Cancellation preserves queued identities, and another Begin appends to that queue.

Two immediate-Clear observations and an unhooked control retain281 identical
public markers. Begin at counter1357 arms selection deadline `41219986`, serial255;
Clear removes that request and receiver while preserving the periodic deadline
and serial49. No selection callback follows cancellation.

Two reachable VsPlayer observations and an unhooked control retain282 identical
public markers. Begin1357 leaves the retained destination and state2 unchanged.
Poll1360 selects hostile source1343, writes state3 and flags1005, publishes
`(-1936,-1424)` and records reachable unit bucket2 score40. The selection request
is canceled, while periodic `+84` remains unchanged. VsUnits also selects this
same ordinary unit with state3/flags1001 and bucket2 score40. Separate unreachable
VsPlayer and VsUnits observations put `(1000,1000)` into bucket5 score9696 and
select no primary target. The latter score is wrapped integer squared fine-grid
distance, not a path cost or evidence that the selector excludes ordinary units.

Each owner side has six buckets. The requested player's categories are0/1/2;
other players use6/7/8. An unreachable query adds3. Category0 checks Unit248.bit40
or rawcode `ugol`; the exact semantic name of bit40 still needs evidence.
Category1 uses `Unit_IsStructureForSupportRecovery(1)`; other units use category2.
`MoveBridge_QueryDistanceWithOwnerExclusions` runs with5000 work and collision
class0. The existing engine `G_MoveOwnerDistance` already implements this query
and its owner exclusions; a new nearest-distance approximation is unnecessary.

The original ranking suffix runs under Unicorn with constructed canonical
registry/Unit objects and the actual native identity resolver and Unit casts.
It starts after `RandData_RollDice` and stops before target publication or
transport dispatch. All49,152 combinations of twelve candidate-presence bits,
six raw flag settings and the two random-result branches match the recovered
preference model. This is an analysis oracle, not producer, RNG-stream, transport
or engine parity.

For raw flag80 clear and die result1, reachable preference is2,1,0,8,7,6; die
results2/3 give2,0,1,8,6,7. Flag80 set changes these to1,0,2,7,6,8 and0,1,2,6,7,8.
Flags100 or200 permit each corresponding unreachable bucket immediately before
its reachable bucket. Native stream13 supplies the1d3 roll. The selected bucket's
reachability feeds transport handling; stage-point and town policy also influence
the final point. Those publication and transport consumers need their own native
proof and production regressions before implementation.

`Captain369SelectionEvidence.java` and `Captain369SelectionPolicyEvidence.java`
append recovered names and evidence in Ghidra. Saved readbacks match their
exports. `MapPathfinding.java` records the selection control, FIFO and ranking
scope. The full-suite checkpoint exposed older Build and ItemTeleport fixtures which
queried the global earliest timer. Their exact-owner corrections now pass all
eight affected tests and480 assertions per edition, with identical production
binary hashes; the corrected full checkpoint is running independently.

A second pair of seven-candidate observations retains331 identical public markers.
The queue visits hrif, Hpal, ugol, hbar, htow, hfoo and hsor in reverse creation
order, and processes them at counters1360,1364,1367,1370,1374,1377 and1380.
The actual stream13 die result1 is recorded at1380, after the final candidate;
the selected Footman1609 has bucket2 score2. Observed Unit248 words are48 for
Town Hall,8 for Barracks, and a for Haunted Gold Mine; the last uses the explicit
ugol category0 exception. The unhooked control matches all331 public markers. Together with immediate
Clear and reachable selection, the three repeated/control scenes cover894 markers.
An ownership round trip leaves the native class query order unchanged, requiring
an immutable birth sequence separate from the engine's mutable owner order.

`Captain369SelectorEvidence.java` saves the owner-mask call chain and a new
`WC3CaptainSelectionPrefix` datatype through208, without changing the original
128-byte prefix. The pending head/tail, twelve weak buckets and unsigned scores
are explicit; unproven bytes remain undefined. Saved readback matches. The engine
currently has an owned-pool sequence which changes on ownership transfer; a
global class enumeration must not assume that it is a birth sequence.


The native SetRandomPaths callback9bfab0 toggles Town2d0 bit80000. Selection
uses that bit to adjust a selected structure's destination through9b8690 and
region tables, consuming another stream13 draw in9bdad0. SetMeleeAI itself only
sets the separate melee bit4000. Region-table construction still needs recovery before this destination branch is implemented.
The pending owner cold-save and transfer-order regression is in the normal C
suite; the strict verifier passes its stated ordinary-selection scope.


The frozen strict fixture now covers five scenes (immediate Clear, ordinary
reachable selection, seven candidates, an ownership round trip, and selected-primary loss), with ten
observations, five controls,1,533 public markers and3,740 original instruction
guards. Seventeen verifier tests reject missing captures, changed owners, timing,
random streams and candidate ordering. The replacement for the older immediate
nearest-member test passes, and the full Captain group passes38 tests and1,532
assertions in each edition. Initializer9c7b20 already
proves Town defaults80181 outside Neutral Aggressive, including random paths.


Primary loss9d2700 resolves follow into primary, restores policy1/range200 and
sets flag2000. If state3 still has no primary, it begins the saved query again.
Two native RemoveUnit observations and a control retain307 identical markers:
selection1360 is followed by synchronous rearm1424 and empty-query expiry1427.
An isolated compiled78838 test plugin records four genuine failures among16
assertions per edition before the recovery implementation. Its447 DWARF ABI
checks, loaded-library path check and52 source/header pins are preserved. The
normal C regression and narrow inverse implementation pass both editions;
no additional save fields or TODO children are needed. The Town preferred-primary
override9d2300 remains a distinct open consumer within the same leaf.
