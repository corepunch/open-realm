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
Both first and repeated applications retain the public Move head851986. Physical
trajectories and disabled-policy behavior remain unverified; public head retention
does not establish that motion continues. Failed/incomplete earlier captures are
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
