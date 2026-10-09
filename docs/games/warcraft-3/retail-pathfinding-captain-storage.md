# Ordered Captain roster storage

Payoff157 changes construction storage while preserving the existing Captain
recruitment and encounter policy. It does not close GROUP-03.4.6.2.1.2 or certify
automatic temporary enrollment.

## Contract and representation

Retail `CaptainAI_AttachRosterMember` (`6f9cf680`) prepends a retained roster
link. Recruitment selects the newest eligible owned insertion first, so edict
index is not the selection key. `CaptainAI_DetachRosterMember` (`6f9d5610`)
unlinks one member and preserves the surviving encounter order. Existing frozen
pair and thirteen-member movement witnesses establish these observations.

The engine previously allocated `count+1` pointers and copied the whole roster
for every recruit. Building N members therefore allocated and copied O(N²)
pointer storage. The roster now retains an allocation base and geometric capacity;
the live contiguous array occupies its end. Prepending normally decrements the
live-array pointer and writes one entry. Growth copies the live entries into the
end of a larger allocation. Construction allocation and copying are O(N) over a
batch, with amortized O(1) insertion. Consumers still see the same contiguous
encounter sequence; no order, identity, RNG or movement callback is deferred.

The allocation base is derived runtime storage. Captain reset frees the base,
rather than the possibly interior live pointer. Standalone existing fixtures
with an ordinary allocated array are migrated when first grown and freed normally
when reset. No edict, network or saved field layout changes.

## Regression and validation

`wc3_bot.captain_roster_bulk_storage_is_linear_and_keeps_owned_order` exercises
actual AddAssault with 256 units. A genuine owner round-trip moves the oldest
unit to the front of the owned pool; the test checks every resulting roster
entry, middle withdrawal, subsequent recruitment and complete Captain reset.
An allocator observer records all allocation calls/bytes during recruitment.
Before the fix, the allocation-volume and allocation-count assertions fail,
while all encounter-order assertions pass. Afterward, all 521 assertions pass in
Classic and TFT. Both runs record four allocations totaling 3840 bytes. The previous code
requested 256 allocations totaling 263168 bytes for this batch (derived from
its allocation loop, rather than attributed CPU samples).

Run with:

```sh
make -j4 game openwarcraft3-tests
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test 'wc3_bot.captain_roster_bulk_storage_is_linear_and_keeps_owned_order'
build/bin/openwarcraft3-tests -data build/tests -tft +dedicated 1 \
  +test 'wc3_bot.captain_roster_bulk_storage_is_linear_and_keeps_owned_order'
```

Original assembly and failing/passing logs are under
`/GitHub/wc3-analysis/runtime/payoff157/`. Existing complete retail travel
witnesses remain the behavioral reference; this change introduces no new
gameplay rule.

## Remaining work

The existing recruitment loop repeatedly scans entities and retained membership;
its computational cost remains unsuitable for large battles. Removal still shifts
the surviving array. These costs require ordered ownership/membership indexing,
with correct mutation boundaries, rather than another allocation optimization.
This change does not establish the 4096-unit frame budget or complete Captain
storage/lifetime integration.

The new public temporary enrollment regression still reproduces three missing
membership assertions. Its source and red log are retained with the runtime
artifacts while enrollment is developed. An instruction-level correction is saved
in Ghidra and the mapper: `9cce50` is the post-attach JMP inside `9ccdb0`, not a
separate caller. Enabled policy returns before order replacement when the unit
already belongs to the attack Captain. Other paths call `6803f0(unit,1)` then
`693450`, which appends internal command `d0006`. Two complete public repeats
and an observer-free control preserve all 90 markers: seven applications cause
three new attachments, while duplicates do not attach or append an AI order.
Both first and repeated applications retain the public Move query value851986.
Payoff158 subsequently distinguishes identity: first/new enrollment replaces the
head with another Captain-owned Move; duplicates retain the actual identities. Physical
trajectories and disabled-policy behavior were outside Payoff157;
[Payoff158](retail-pathfinding-captain-enrollment.md) integrates the observed
enrollment/detachment policy. Numeric query retention alone does not establish
that motion continues. Failed/incomplete earlier captures are
preserved and do not count as parity evidence.

The frozen contract is `tools/ghidra/fixtures/retail-captain-enrollment-live-1.27.json`.
The read-only observer and public producer are `tools/frida/research/captain_enrollment157_*`.
The independent control uses no injected script. Verify the completed archived
captures with:

```sh
python tools/ghidra/research/verify_captain_enrollment157_live.py \
  --captures /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/captain-enrollment157 \
  --expected tools/ghidra/fixtures/retail-captain-enrollment-live-1.27.json \
  --report /tmp/captain-enrollment157-new.json
```

The controller resumes the owned process before attaching to accommodate Wine
loader initialization. It requires the four observed startup actor identities,
so a late attachment is rejected. Loading-screen advancement uses a mouse click
and Space; keyboard-only earlier attempts never completed the public producer.

## Larger rosters and constant-time quorum (Payoff161)

The physical request limit is twelve members, independently of the logical
Captain roster. The engine previously restricted retained physical references
and construction scratch to thirteen recruits. Actual creation of24 and25
Footmen reproduced70 failed assertions: larger members lost Captain admission,
and the resulting roster could not retain its movement/save contract.

Move now collects the complete logical roster into request-local storage and
retains separate twelve-member physical requests:24 produces12+12,25 produces
12+12+1. Nested order calls cannot overwrite this request-local storage. Physical
member indices retain their existing order, independently of AI prepend order.
Target and point request builders share this collector. The larger public live
witness covers point publication; larger target combat trajectories remain open.

Ordinary range-entry/exit edges maintain `botCaptain_t.entered_members`.
Withdrawal subtracts an entered member before removing its logical reference;
physical order replacement retains membership. Ordinary callbacks read this
count in constant time instead of scanning all entities. Standalone and retired
actors use the tracked movement set. Cold load reconstructs the derived count
from saved logical membership flags. No serialized field or save-version change
is needed. Duplicate `(actor, member index)` validation sorts keys in O(N log N),
replacing the previous pairwise O(N squared) scan.

### Retail evidence and regression

Two complete repeats and an observer-free public control for each of24 and25
Footmen retain2,567 public markers and218 prepared-member events. Native9d1040
increments local counts modulo12 and increments the physical request index on
wrap. All members of one publication share the same retained shared-parameter
identity. The25-member first publication reaches request index2/count0. Removal
then preserves the surviving24-member12+12 publication. The24-member speed word
is43870000;25 is438877a6 before all-entry refresh, then43870000. There is no
post-multiplier clamp to the roster minimum.

The actual engine regression
`wc3_bot.captain_large_roster_preserves_admission_batches_and_cold_save` covers
both sizes, every retained index, a cold save before motion, entry into shared
requests, a second cold save, middle withdrawal and point replacement. It fails
before the implementation and passes afterward. Classic/TFT each pass104 bot,
193 save and373 movement tests; the movement suite has5,226,344 assertions.
The fixture resets both simulation and retained path clocks between scenes;
resetting only `level.time` incorrectly forces unsigned clock catch-up.

Read-only hooks, map construction and frozen results are:

- `tools/frida/research/captain_roster161_observer.js`;
- `tools/frida/research/captain_roster161_make_map.py`;
- `tools/ghidra/fixtures/retail-captain-roster161-1.27.json`.

The public capture controller is `captain_enrollment157_capture.py`; it resumes
before attach and uses mouse plus Space to advance the loading screen. Earlier
loading-only and failed Frida-attach runs are preserved but excluded. Completed
captures/maps are archived under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/captain-roster161/`.
Failing/passing engine logs and assembly excerpts are under
`/GitHub/wc3-analysis/runtime/payoff161/`. Verify the archived evidence with:

```sh
python tools/ghidra/research/verify_captain_roster161_live.py \
  --captures /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/captain-roster161 \
  --expected tools/ghidra/fixtures/retail-captain-roster161-1.27.json \
  --output /tmp/captain-roster161-new.json
```

Ghidra comments at9d1040,9d27c0,9d9020 and9d4c20 were saved and mirrored in
`MapPathfinding.java`. Larger original target/combat motion, special-unit counters,
default homes and complete retreat/callback composition remain open under
GROUP-03.4.7.3. Recruitment and withdrawal indexing still require work; this
change does not establish the4096-unit frame budget.
