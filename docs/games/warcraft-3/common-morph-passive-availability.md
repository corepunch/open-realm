# Configured common Morph availability

The CommonMorph374 experiment covers ordinary Bear Form (`Abrf`) and Storm Crow
Form (`Arav`) under `Misc/MorphAlternateDisable`: the omitted setting and authored
zero return policy zero; authored one returns policy one. Submerge's unconditional
policy is documented separately in [Submerge availability](submerge-passive-availability.md).
This work remains within the existing SEP-01.2 common Morph investigation.

The accepted portable corpus contains two complete instrumented observations and
one independent uninstrumented control for each setting. Each setting has six
scenes: the two families with preexisting Permanent Invisibility (`Apiv`), a late
passive attachment, or an initially alternate unit. The enemy observer explicitly
clears passive alliance, shared vision and shared control in both directions.
Preexisting-passive scenes positively report enemy invisibility before the
forward order. All public rows remain literal.

Accepted archive inputs under `common-morph374` are:

| Setting | Observation | Repeat | Control |
|---|---|---|---|
| omitted | `viewer-default-observe-a.jsonl` | `viewer-default-observe-b.jsonl` | `viewer-default-control-a.jsonl` |
| zero | `viewer-0-observe-a.jsonl` | `viewer-0-observe-c.jsonl` | `viewer-0-control-a.jsonl` |
| one | `viewer-1-observe-a.jsonl` | `viewer-1-observe-b.jsonl` | `viewer-1-control-a.jsonl` |

The zero-setting `viewer-0-observe-b.jsonl` failed before a complete preload and
is retained as a diagnostic. The original `default-observe-a.jsonl` failed during
loading; `0-observe-a.jsonl` and `1-observe-a.jsonl` use the earlier allied-viewer
setup. Their original files and capture sources are preserved without edits.

The three reference observations contain 1,320 public markers, 1,494 native
events and six literal pre-start system exception rows, yielding 2,820 projected
events. Each of the six instrumented observations reports two pre-start system
exceptions outside game.dll before active markers; complete public and native
footers follow. The evidence does not establish the cause of those exceptions or
OS fault unreachability. Four original failed/shared-viewer captures remain in
the portable bundle as diagnostics. The accepted captures are not described as
exception-free.

With policy one, Bear acquires its passive suspension after the deferred forward
completion. Raven acquires during the immediate takeoff type rebind, before the
common forward begin/public order return. Initial alternate attachment acquires
synchronously. The passive remains suspended across the alternate form and the
reverse transition. The authored transition delay is exactly `.375`; Raven's
landing adds `1.0`, giving `1.375`. Reverse completion clears the same counted
availability contribution and restarts the passive's `.625` fade. Raven's early
post-reverse sample at age 50 is still visible to the enemy; the later age-55
sample positively confirms invisibility. Bear's corresponding first positive
sample is age 45. An age-50-only check would misclassify the landing/fade sequence.
Policy zero preserves passive availability through the form. The ordinary forward
order can reveal the unit and restart its fade; type rebind then completes the
retained contribution immediately.

Only Unit offsets `0x128` and `0x12c` are projected as relocatable object handles.
The other four words at `0x118..0x124`, stable unit identities, counters, clocks,
sequence numbers and exception contents remain literal. Complete guarded
functions `6f3e3c00` and `6f3e3bd0` cancel through pointee virtual slot `+8`, release
pointee refcount `+4`, destroy at zero, and store literal zero through the handle.
Callers `6f6696e0`, `6f694b20` and `6f68b780` pass the exact two Unit field
addresses. `6f058120` supplies the allocation proof. The Ghidra annotation was
saved and exported again; the readback matches the complete export.

Canonical identities persist across every concurrently observed holder, including
aliases between the two fields and across units. A token retires only when its
last observed holder becomes literal zero. A reused address then receives a new
monotonic token. Nonzero-to-different-pointer replacement and unfinished live
holders are rejected. The setting-zero repeats retain 15 rows of original
whole-capture allocator variance, setting-one repeats retain 23, and the default
repeats have none. All differences disappear under this proved lifetime contract;
no repeat was selected to conceal variance. Original raw JSONL bytes and both
literal retirement ledgers remain pinned in the bundle.

The complete same-unit rebind function `6f670950` directly calls the refresh at
`6f670d54`. It clears the existing fade objects and publishes scalar one when
contribution `0x114` remains nonzero, before later ability replacement. The engine
captures pre-rebind attached/enabled Apiv locally and refreshes only that retained
owner after rebind. A passive newly authored only by the destination type keeps
its newly started fade. Suspended initialization cannot reacquire the source;
normal reverse completion and actual detach restore the base while the Morph
owner remains held, then release it and restart the authored passive fade. The
same inverse order preserves Burrow and Submerge's existing regression contracts.
No serialized field or generic event payload was added. The later authored-ability
replacement walk (`6f694f00`/`6f6947e0`) removes an unretained zero-policy passive
through `6f48e8a0` and its virtual detach/retirement path. Concrete Apiv slot
`+0xe0` points to `6f62e600`, which removes the contribution with zero transition.
The existing engine cancellation owner already handles this removal: a normal
rebind dispatches the full `A_UNIT_INIT` path, including innate Apiv initialization.
An absent passive makes `permanent_invisibility_transition` return negative;
initialization removes its timer and clears the contribution. A pending Apiv timer
also prevents the fresh-unit initialization shortcut. Both settled and pending
return-to-base regressions pass, including checks past the former fade deadline;
no additional removal behavior was applied.

The policy's integer lookup `6f54c640` calls `6f047710` with default zero;
`6f1a8d10` uses `6f1a8950`, whose numeric branch parses base zero and whose quoted
branch packs up to four bytes. Common acquire/release compare the full result
against zero. Engine truth testing therefore accepts nonzero integers rather
than only literal one; no textual-true grammar is inferred from these captures.
The portable runtime settings specifically cover absent, zero and one. Complete
parser/rebind functions were exported, saved, and read back again. The initial
apply export's caller pseudocode used the old parser name until decompiler cache
refresh; instruction bytes were unchanged, and two subsequent readbacks match.

`verify_wc3_pathing_morph374.py` checks all scalar/public projections, source,
controller and map pins, 3,513 instruction guards and four concrete Apiv vtable slots, positive enemy visibility,
family-specific acquisition frontiers, exact timers, inverse counters and later
fade completion. It rebuilds all three maps deterministically. Native-only mode
certifies this evidence, not engine parity. The Python integrity suite exercises
16 complete-contract and lifetime tamper checks.

The regression fixtures load real nested map MPQs containing `war3mapMisc.txt`
through `gi.SetPriorityArchive`, then use the production `miscdata_files` list and
`Stb_IniCacheLoadFiles`. The absent, zero and one archives are distinct inputs;
no internal policy switch substitutes for configuration loading. The tests cover
both data schemas, initial/late/preexisting passive attachment, takeoff/deferred
completion, landing, detach and saved ownership. Save coverage uses `WriteGame`
and `ReadGame` in the same process while alternate or reverse-in-flight; it does
not claim restoration after process restart or across a different map archive.
The genuine failing-first run passed 5,216 of 5,438 assertions in two tests in
each edition: 198 failures concerned the missing configured owner or its inverse
fade, and 24 concerned zero-policy retained-passive fade completion at rebind.
Literal absent/zero/one configuration, schema/flight setup, order admission,
save/read, detach restoration and final fade setup checks passed. The 24 rebind
assertions were retained. The fix adds explicit no-passive eligibility, positive
zero-policy publication, reverse held-fade absence, and a destination-only
passive test. The first corrected build fixed all 24 zero-policy rebind failures and passed the
destination-addition test. It still failed 29 of 5,466 assertions per edition:
27 came from the RoC fixture mistakenly putting rank-one slot C in `Data31`
instead of `Data13` (GDB showed a `.375` deadline instead of `1.375`), and two
came from early detach materializing only the detached owner before its forced
completion. Existing Submerge tests exposed the same missing-passive walk in 12
assertions per edition. The correction primes the attached availability records
before creating the first detach owner; the tests retain their fade assertions.
All five Burrow366 tests passed 1,646 assertions per edition in that build.
The second shared strict checkpoint passed 26 tests and 13,004 assertions per
edition, including Burrow366, Submerge372, common Morph, separation-policy and
save gates. The added pending-fade removal baseline passed 96 assertions per
edition with the production helper unchanged. The final shared strict gate
passed 26 tests and 13,052 assertions per edition; the separate pause288/pause289
counted-callback gate passed 10 tests and 3,115 assertions per edition. The final
strict report is `verified-shared212-final.json` in the external capture archive.
The isolated postmerge production/test build passed 26 tests and 13,055
assertions per edition in `root-private-postmerge-final.json`. The three extra
assertions come from the upstream save-format gate; the Morph assertions are
unchanged. Native guards, all nine captures and 2,820 projected events also pass
in that isolated run.
