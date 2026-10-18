# Scripted pause owners release before removal returns

A pending removal still owns its canonical unit and attached ability records.
Canceling its scripted pause must release the contribution from every applicable
attached owner before removal cleanup continues.

The complete retail `48c8c0` inverse walk captures the next ability identity before
calling the current owner. It does not stop merely because the unit is dead or
pending removal. The per-ability inverse `40bb00` subtracts the order counter40
before the availability counter3c. Physical destruction and a new unit incarnation
remain separate lifetime boundaries.

The existing frozen removal304 family supplies two complete Frida observations
and an unhooked control, with all36 public markers matching per capture. In its
paused idle and moving scenes, Inventory, Defend, Attack and Move each have
counters1/1 at `RemoveUnit` entry. All four have0/0 before the first synchronous
cleanup, at native return and at scheduled inactivity entry. Final retirement
then reaches Move's first disable with1/0, before its order counter increments.
The original capture bundle and producer pins remain unchanged. The379 verifier
adds48 explicit owner snapshots and506 original instruction guards from12 complete
functions, saved in Ghidra with an identical readback.

Open realm previously decremented the global pause contribution, released the
first owner and stopped the inverse walk because deferred removal was already
registered. Later owners retained stale pause counters. The dedicated public
warm/cold regression passes61/79 assertions before the fix, with18 failures;
the original removal304 tests retain four failures in the paused final-retirement
cases. The minimal engine fixture has three applicable owners; its count does
not claim equality with retail's four-owner inventory.

The fix lets the inverse visit the remaining retained records while preserving
its physical liveness, incarnation, linked-record and held-next-record guards.
The acquire walk keeps its existing guards. A separate synthetic callback frees
and reuses the source through the real allocator lifecycle, protecting the
incarnation boundary; it is labelled engine lifetime protection rather than
another retail producer. Its corrected baseline passes17/17. The first attempt
incorrectly expected immediate slot reuse despite the allocator's1000ms
quarantine, and its four fixture failures are preserved as diagnostics.

This work remains within the existing SEP-01.2 leaf. It does not establish the
remaining counted availability producer inventory or retail save-file parity.

The final native and engine gate passes45 tests/10,755 assertions in each of
Classic and TFT. The new public/lifetime tests pass96/96; unchanged removal304
now passes614/614, including the four preserved final-retirement assertions and
the additional callback checks that execute when those recoveries occur. All
scope300, pause287/288/289 and unit-release regressions pass. Six integrity
tests reject altered owner counters, reduced native inventories, missing
controls and incomplete original instruction guards.
