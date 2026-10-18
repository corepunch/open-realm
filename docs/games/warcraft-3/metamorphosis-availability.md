# Metamorphosis availability and typed buff lifetime

Payoff382 covers the concrete `AEme` producer and its source-linked `BEme` buff within SEP-01.2. Authored BuffID remains the public raw identity; concrete class identity independently selects lifecycle and internal mechanics. A `BEme` authored as `Bblo` or `Amov` retains that public name without acquiring Bloodlust bonuses or supplying the Move capability. Existing class-zero statuses retain their previous identity behavior.

The accepted retail family has 36 captures, 38 scenes, 3,790 public markers and 8,470 projected events. Whole-function static guards cover 5,271 instructions and 35 data ranges. All twelve map rebuilds are required. Ordinary, initial-alternate, late passive attachment, forward/alternate detach, natural inverse, public buff removal, authored aliases, ScriptPause and Sleep composition have separate positive producers and complete repeats/control.

The concrete forward callback creates a real typed BEme with its own 2.51-second primary timer; the common 2.5-second lifetime and .375-second inverse are separate owners. ScriptPause suspends the private clock and retains common lifetime remainder. Physical Sleep suspension does not suspend that private clock. Public buff removal restores the base synchronously; natural expiry requests the observed timed inverse. Initial alternate configured AEme retains its observed 3c=1/40=0 ownership quirk. Nonpositive HeroDur retires the prior buff before skipping replacement creation. Other morph classes and health-bonus parity are outside this producer claim.

Only Unit offsets 0x128 and 0x12c are projected as relocatable lifetime pointers. Native reference-count decrement, virtual cancellation/destruction and zeroing establish retirement; canonical tokens preserve concurrent aliases and only permit address reuse after the last observed holder clears. Every other scalar word, public marker, class/source identity, clock, sequence and exception row remains literal. The original compressed capture bundles and diagnostic failures are retained byte-for-byte.

Every observed capture retains two prestart reported-system-exception rows outside game.dll (24 rows per reference family, 48 across both observations). The handler returns false. Observer-free controls complete with the same public stream; the cause and any instrumentation contribution remain unproved. These captures are not described as exception-free.

Save coverage is an in-process cold object-graph round trip: cross-references, class identity, counted owners and primary timer reconstruction are checked after load. It does not establish cross-build save compatibility. The integrated save format is216; older layouts are rejected.

Failing-first records are preserved separately: original producer/alias/Pause/Sleep gates; the real producer-backed pooled-slot release crash; cached/uncached Bblo class confusion; and public Amov alias versus actual Move capability. The corrected ScriptUnpause fixture runs its real physical owner timer without changing the clock/fade assertions. Historical 1,448-failure and intermediate 80-failure reports remain immutable, with their fixture failures classified instead of promoted as behavior evidence.

On the exact672 baseline, the unchanged final alias/lifetime tests failed 56 assertions per edition before class-query correction, then passed all 12,434 assertions across eight tests; the pooled-release case passed 376 assertions separately. Rebased on82c9, the broader affected gate passes 3,144 tests and 9,502,858 assertions per edition. The unchanged engine candidate passes all34 native suites (9,922 cases/40,423,411 assertions). After corpus registration and reviewed source-pin updates, all2,818 pathfinding Python tests pass. The exact SC2API CI command also passes; transport retains its existing optional skip. The registered strict engine gate requires39 tests/31,842 assertions per edition. No whole SEP-01.2 closure is claimed.

Reproduce the bounded retail/engine gate after building the debug test runner
and test assets; use a new report path:

```sh
python3 tools/ghidra/verify_wc3_pathing_morph382.py --binary /path/to/game.dll --report /tmp/morph382-fresh.json
make BUILD=debug SC2API=1 PROTOBUF=0 test-pathfinding-tools
```

See also [the corpus](retail-pathfinding-corpus.md),
[remaining work](retail-pathfinding-todo.md) and
[research handoff](retail-pathfinding-handoffs.md).
