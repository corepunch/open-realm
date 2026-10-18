# Mixed target event ownership (Payoff363)

`GROUP-03.4.7.3` remains open. Payoff363 implements the common event registration
history for Move, Attack and Captain target owners. Inventory/Missile ordering,
other Captain policies (including the complete Town/readiness callback admission),
complete trajectories and retail save/load remain within
the existing parent and BASE/E2E scopes.

## Retail contract

For retail1.27 `game.dll` SHA256
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`,
Captain `9d4a50` registers target-lost `d01a4`, death `d01a0`, then owner-change
`d01a2` through the common `0725d0` registry. All three use the same concrete
Captain callback identity. Multiple logical roles for the same Captain and unit
do not create extra callbacks. The key is **event plus callback**, so subscribing
to a new event later must retain its own position even when another event already
has a registration for that owner.

The common dispatcher `071e00` delivers each event in append order. Duplicate
registration updates its existing record; removal tombstones pending delivery.
The current pass excludes registrations appended during a callback, while a
nested dispatch sees those registrations. The unchanged original-code
ORDER-03.1 oracle passes627 cases, including361 nested deliveries. Move's actual
approach-to-Follow handoff explicitly retires and renews its target callbacks;
this concrete transition therefore changes its registration position.

Nine owned retail runs cover public `RemoveUnit`, `SetUnitOwner` and `KillUnit`:
two read-only observations and an observer-free control per scene. Each has302
matching public markers,2,718 total. The complete synchronous mutation windows
contain137,142 and159 records respectively,438 total. Both observations match
every record after removal of wall time, replacement of process pointer addresses
by encounter identities, and explicit relocation of Captain FloatMini metadata
`a9de14`. Scalar state, object identity relationships, clocks, nested messages,
subscriptions, Inventory and Missile callbacks all remain in the comparison.

The observed typed callback order is an early independent Move, six roster
Attack owners, the Captain, an independent Attack, then a second independent Move.
The previous engine delivered all Move owners before all Attack owners and reached
the Captain only through eventual destruction. Retail clears the Captain source
roles and updates its active request synchronously. `KillUnit` publishes
TargetLost before Death; the Captain unsubscribes during TargetLost and is absent
from the subsequent Death ring. The dying source can select a different Captain
retreat result than removal, so these are separate assertions.

Primary RTTI identifies the other actual listeners: `b28330` is
`CAbilityInventory`, callback `561680`; `addfb8` is `CMissile`, callback `6d4e30`,
with TargetLost virtual `b8` reaching `6d6f80`. Their complete native context is
frozen; the engine registry here covers the three typed path/combat owners.

## Engine ownership and checkpoints

`game/g_target_events.c` owns one pointer-free logical registration per target,
typed owner and event. It maintains a common target FIFO, an owner index and a
hash. Pool and hash storage grow geometrically; there is no fixed subscriber cap
below actual owner demand. Dispatch snapshots retain node number and registration
sequence rather than pointers into growing storage. Retired slots can be reused
immediately without reviving pending outer deliveries. Nested callbacks retain
their own snapshots and see newly registered listeners.

Move and Attack publish registrations through their existing target setters.
Captain registrations are the deduplicated union of actual roster, primary,
follow and backup references. `G_BotCaptainTargetEvent` performs the concrete
per-Captain inverse, including dependent member Attack cleanup and retained point
restoration. It can also be invoked by the independently recovered
[ClearCaptainTargets policy](retail-pathfinding-captain-clear.md).

Save208 writes the global registration sequence and32-byte records containing
sequence, target/owner identities, both entity incarnations, owner kind and a
single event identity. FIFO/hash/free links and dispatch snapshots are derived.
Load validates event membership, identity/incarnation, rank uniqueness and exact
correspondence to the restored logical owners, then rebuilds links in saved order.
It rejects duplicate callbacks for one event and incomplete or stale ownership.
It never reconstructs distinct event positions from one owner-wide rank.

Public `SaveGame` keeps its existing admission: `G_RequestScriptSaveGame` queues
the request and `G_FlushScriptSave` writes the checkpoint after synchronous JASS
callbacks have returned. The registry adds no active-callback save rejection.
Full world replacement clears registry storage; resetting only derived ability
timers does not discard live target ownership.

The allocation observer in the roster storage regression now includes three
Captain subscriptions for each member. Its former roster-only32KiB budget fails
with126,720 bytes/12 allocations for256 recruits. The original failure is retained.
The updated regression measures independent128/256 worlds, bounds the growth
ratio and allocation-call increase, and keeps a640-byte per-recruit budget derived
from geometric node/hash/roster allocation volume. All roster order checks remain. Classic/TFT each pass525 assertions:128 recruits
use11 allocations/59,211 bytes and256 use12/126,720.

## Verification and reproduction

The accepted pre-fix actual engine fixture passes all setup/admission/save checks
but fails28 of112 behavior assertions in both Classic and TFT: the original16
source cleanup failures, eight mixed FIFO failures and four death cleanup failures.
Earlier captures with inactive map-player fixtures are preserved and excluded;
`Move` admission requires an active target owner, independently of visibility.
Read-only engine Frida diagnostics verified the Move ability and visibility before
the fixture's missing `playerType` was isolated. Production fog behavior was unchanged.

After implementation, the strict verifier passes22 tests/970 assertions per
edition. Coverage includes all three public mutations, warm/cold checkpoints,
independent event ranks across save, duplicate updates, nested dispatch with100
new real Move owners, cancellation and slot reuse during the outer delivery,
corrupted saved identities/events, and the existing target167/223 mutation suites.
The full Classic `wc3_save.*` suite passes224 tests/68,041 assertions. Eleven Python tests
reject missing repeats, hooked controls, changed provenance, shortened callback
windows, reordered callbacks and altered Captain scalar state.

```sh
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2:build/lib \
python tools/ghidra/verify_wc3_pathing_target363.py \
  --binary /path/to/retail-1.27/game.dll --report /tmp/target363-fresh.json
python -m unittest discover -s tests -p test_wc3_pathfinding_target363.py
```

The report path must be new. The verifier guards1,233 instruction rows in15
complete Ghidra functions, original RTTI callback classes, source hashes and the
portable nine-capture bundle. `Target363Evidence.java` applies the same mapper
notes in Ghidra, saves the program and supports an independent readback.
Raw observations, rejected diagnostics, accepted failing logs and fresh reports
are under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-03.4.7.3/payoff363/`.
Retail capture uses the isolated `_env/live.sh` B/C locks, the owned
`target363_capture.py`, `target363_registry_observer.js`, and public probe maps
built by `target363_make_map.py` / `target363_death_make_map.py`. Every accepted
run uses the actual preload `rs-target363.txt`; the generic map builder's old
`rs-group0473.txt` metadata is not the runtime evidence.

See also [Captain policy](retail-pathfinding-captain-policy.md),
[roster storage](retail-pathfinding-captain-storage.md), and
[save/load ownership](save-load.md).

Final isolated Save208 validation compiled both production and test modules from
the reviewed Git index: 22 tests / 970 assertions per edition, plus the full
save suite (224 tests / 68,041 assertions per edition), 11 target-event integrity
tests, 38 corpus tests and 12 exclusion-owner tests. All passed.
