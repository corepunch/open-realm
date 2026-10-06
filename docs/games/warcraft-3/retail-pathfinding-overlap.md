# Complete overlap separation and constant-time owner retirement

Payoff133 closes SEP-02.3 and SEP-04.1. It builds on the independent
[ordered proximity index](retail-pathfinding-proximity.md): all 20 phase-O
units run together through the production `CAbilityMove(A_OWNER_UPDATE)`
scheduler. Nineteen are active repulsors; the disabled unit remains present
in occupancy. Across 268 owner passes, every one of 2,546 visits and 1,882
individual neighbor calls matches the unchanged retail captures.

## Shared stream and admission

The test supplies the captured initial owner words `4273436052/209508436`
and alternating-list phase 1. Subsequent visit order comes from the engine,
not a scripted list of expected units. All 1,476 draws occur in the same
global order and end at `3711717831/2080948460`. Every individual contribution
checks source/candidate identities, positions, before/after vectors and RNG
words. Complete visits check retained vectors, policy/cooldown, attempted
endpoints, admission, committed fine positions and occupied rectangles.

This distinction matters: extracting a pair from the scene changes its
second draw, because other clusters consume the shared stream between visits.
The retail startup eight race-resolution draws are retained in the evidence;
production of the map-start seed remains NUM-04.5. The supplied state does not
close that prerequisite's engine integration.

An exact overlap at a cell center need not separate. The retained displacement
can repeatedly fail class-sized endpoint admission while later contributions
continue consuming RNG. Selector-zero rows still draw for tiny-overlap neighbors,
then zero the accumulated vector without installing cooldown. The corner O9
pair has 268 visits, including 61 bodies and 207 cooldown visits: 30 endpoint
acceptances, one rejection, two random draws and eventual rest. Tests retain
blocked controls, rejected vectors, cooldown visits and zero-contribution calls.
No scene-specific escape rule is added.

## Engine payoff: intrusive owner slots

The old singly linked repulsor list required a scan from the head for each
pause, channel, removal or type rebind. Retiring 1,024 units in creation order
visited 524,800 owners: quadratic work. A new failing production-pause regression
established that cost before changing the implementation. Frida measured the
native regression's work counter both before and after the fix.

Move now caches each active unit's incoming link slot outside the edict.
Prepending repairs the old head's slot; unlinking redirects the incoming slot
and repairs the successor's slot. Each warm retirement costs O(1), so the same
batch visits exactly 1,024 owners, 512.5 times fewer visits. Owner order, phase,
callbacks and RNG consumption remain unchanged. This is a reduction in this
operation's work, not a claim of a 512-fold frame-rate improvement or completion
of the overall performance target.

`S_RestoreMoveRepulsors` reconstructs and validates these derived slots after
load. It rejects out-of-range/misaligned owners, cycles, inactive linked units
and active orphans. Empty runtime lists initialize without a whole-edict scan;
load still performs full validation. Timer/map resets discard the cache.
Save 119 already stores the authoritative head, next pointers, phase and state;
no cache pointers are saved, and neither edict nor network layout changes.
Regression coverage includes a real save/load, middle unlink, resumed prepend,
successor repair, cycle rejection and orphan rejection. The test entity-reset
helper now clears its stale owner head/phase when it clears all edicts, matching
production map reset.

## Evidence and reproduction

The existing portable proximity bundle contains both complete raw Frida
captures, replay inputs, map roster and observer-free control. The unchanged
`SEP-02.3-expected.json.gz` has uncompressed SHA256
`c6a63ba77761771757bd6a94057c2fd018c46c554f8d4b80e98863af491c8e89`;
`SEP-04.1-expected.json` has SHA256
`436190f8d21bec3f9c0e4e9c82bab3fa734feebfc49e667515d8fcd794b0fd6a`.
The latter's O1 contrast is explicitly a 64-visit truncated prefix. Its complete
268-visit control is checked against the former's full O1 sequence. The O9
primary witness is complete. Expected results have not been edited.

`verify_wc3_pathing_overlap.py` freshly runs the complete original pair/tail
replays for both captures, checks every normalized group and all shared draws,
and compares every C fixture input, visit and pair. Original endpoint results
come from live capture; actual engine admission is checked by the regression.
All 3,543 observer-free markers remain equal. Six Python checks additionally
reject missing visits, discontinuous RNG, reordered neighbors and changed
endpoint outcomes and require the saved Ghidra mapping.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_overlap.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --report /tmp/wc3-overlap-oracle.json
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 build/bin/openwarcraft3-tests \
  -data build/tests +dedicated 1 +test 'wc3_repulsion_overlap.*'
```

Add `-tft` to the engine command for expansion data. The overlap suite executes
three tests and 93,935 assertions. `retail-overlap-ghidra-1.27.json` records five
saved/read-back owner, separation, direction and endpoint functions;
`MapPathfinding.java` retains the reusable annotations. Local before/after
build, Frida and focused validation logs are in
`/GitHub/wc3-analysis/runtime/payoff133/`.

Reproduce the native work measurement against a selected test module:

```sh
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 \
/home/lofcz/.local/share/uv/tools/frida-tools/bin/python \
  tools/frida/research/repulsion_retirement.py \
  --library build/lib/libgame-wc3-test.so --output /tmp/repulsor-work.json
```

This diagnostic measures the tested retirement operation, not total frame CPU.

Accepted focused validation: Classic/TFT release each pass678 tests and
8,399,210 assertions (movement, routing, policy, proximity and save/load);
debug each passes16 separation/proximity tests and427,980 assertions. The
isolated staged tree passes32 Python checks with365 corpus entries/508 pinned
inputs, and the fresh full original overlap verifier reports no differences.
Production and test release builds pass. Full-suite validation remains on the
authorized twelve-implementation-commit cadence.
