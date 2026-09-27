# NightElfX01: Rise of the Naga flow investigation

## Confirmed failure: cinematic trackers trigger the rescue sequence

The September 27, 2026 comparison used the installed TFT `NightElfX01.w3x`
from `War3xlocal.mpq`, retail 1.27.1.7085 under Wine, and OpenRealm with
`skip_cutscene=0`. Retail `game.dll` SHA-256:
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`.
Recordings and Frida traces are retained locally under `build/elves01-audit/`;
they are ignored diagnostic artifacts, not portable test fixtures.

The opening's main dialogue/order sequence ran in both implementations. The
first proven story-order divergence was the cleanup handoff:

| Milestone | Retail | OpenRealm before fix |
| --- | --- | --- |
| Intro cleanup | Removes all fourteen trackers from two seven-unit groups | Skips alternating members and repeatedly removes the last member |
| Player handoff | Maiev's group assembles at the starting gate | Surviving cinematic trackers keep moving elsewhere on the map |
| Next story event without gameplay input | Discover the Illidan main quest | Rescue Archers, Wildkin voiceover, then Wildkin cinematic |
| Main quest | Discovered after the authored five-second gameplay delay | Waits behind the unintended queued rescue/Wildkin sequence |

The live native trace recorded `Trig_Rescue_Archers_Actions` after cleanup
without any gameplay input. A subsequent bounded headless trace identified the
cause in simulation time: cleanup at 37.1 seconds; surviving Archer edict 4244
entered the rescue rectangle at 39.8 seconds, at approximately
`(-3881.88, -5178.20)`. The authored `gg_rct_ArcherRescueGroup01` rectangle is
`(-5472, -5184)` to `(-3520, -4000)`. The main quest was not discovered until
71.0 seconds, after the unintended queue entries.

Edict numbers and timing are observations from that run, not constants to put
in game logic. Maiev's six `SetUnitPositionLoc` calls did reach their authored
destinations: the camera jump was caused by the rescue script, not a failed
teleport. That script pans to `gg_rct_Rescue01Huntress_Dest` when outside a
cinematic, transfers three units, and enqueues the Wildkin dialogue/cinematic.

## Root cause and ownership

`Trig_Intro_Cleanup_Actions` calls `ForGroupBJ` with callbacks that invoke
`RemoveUnit(GetEnumUnit())`. `G_DeferFreeEdict` immediately removes the unit
from every JASS group. `ForGroup` previously walked the mutable member array
while `FOR_LOOP` retained its initial count. Compaction skipped the next
member; later iterations read repeated entries beyond the new logical count.

The headless trace showed cleanup visiting:

```text
first group:  4236, 4238, 4240, 4255, 4255, 4255, 4255
second group: 4241, 4243, 4245, 4254, 4254, 4254, 4254
```

Therefore a count of fourteen `RemoveUnit` calls was insufficient evidence of
successful cleanup: only eight distinct trackers were removed. The identity
of each removed unit matters.

The fix belongs to the generic `ForGroup` native. It snapshots member pointers
and spawn identities before calling JASS, skips removed/replaced entities,
and restores the enclosing enumeration unit after nested callbacks. There is
no campaign-specific branch. Group mutations affect subsequent enumerations;
they cannot shift the active enumeration's remaining members.

The local Ghidra analysis corroborates the snapshot contract in this retail
binary: native registration maps `ForGroup` to `0x6f1fe690`, which calls
`0x6f22ff30`; its iterator at `0x6f22ed70` constructs a temporary retained
array through `0x6f2939f0`, validates each agent's identity before its callback,
and releases the temporary references afterward. These are preferred-image
addresses (base `0x6f000000`); Frida hooks must relocate against the actual
module base. Decompiler output alone is not a complete native ABI declaration.

## Verification and evidence

`wc3_jass_map.nightelfx01_cleanup_removes_every_tracker` recreates the two
seven-unit groups and the real JASS `ForGroup -> RemoveUnit` path using fixture
units. It failed before the fix with `cinematic tracker survived cleanup`.
After draining deferred removals, it checks every original handle and both
groups, rather than only callback counts.
`wc3_jass_map.forgroup_snapshot_survives_nested_mutation` covers nested
enumeration, removal of a future member, group clearing/destruction, and
exclusion of newly added members.

```sh
make test-wc3-engine WC3_PATTERN='wc3_jass_map.*'
make test-wc3-engine WC3_PATTERN='wc3_*'
```

Both ROC and TFT passed 67 JASS-map tests (175 assertions) and 1,944 Warcraft
tests (34,942 assertions) per mode. The aggregate `make test TEST_JOBS=4` run
was blocked by an SDL crash in
`client_input.menu_sdl_input_is_exclusive_with_world_presentation`; that test
also crashed in isolation with `SDL_VIDEODRIVER=dummy`. Its stack and the
aggregate log are retained as `full-test-crash.txt` and `full-test.log`.
The concurrent client-input changes were not modified by this investigation.

Useful local artifacts:

- `retail-intro.mp4`, `retail-traced.mp4`: retail opening and handoff recordings;
- `native-intro.mp4`: original native capture, including the premature rescue;
- `retail-flow.jsonl`, `native-flow.jsonl`: native-call order and timestamps;
- `headless-flow.jsonl`, `causal-excerpt.json`: unit identity, position, region
  entry, and simulation-time evidence;
- `native-addresses.json`, `retail-forgroup-decompiled.json`: retail hook map
  and iterator disassembly/decompilation evidence;
- `regression-before-final.log`, `regression-after-final.log`: automated regression results.

Use the authored cleanup regression for subsequent validation rather than
replaying the opening to re-prove this bug.

## Comparison pitfalls and remaining scope

- `build/parity/wc3 openrealm --map=elf1` sets `skip_cutscene=1`; this also
  shortens later cinematics. Use `--intro` for normal timing in both runs.
- The Xvfb renderer used software rasterization. Raw wall-clock durations are
  not simulation-time parity measurements. Align script milestones and record
  simulation time when available.
- Wine resolved the same installed DLL as `K:\Games\w3\Game.dll`, while the
  existing audio probe expected a `Z:` path. Its initial rejected trace was
  discarded; compare resolved files and hashes before changing a path check.
- An outdated engine executable paired with newer renderer modules crashed
  in loading-screen `R_SelectUISequence`. Rebuilding the engine restored the
  comparison. Those failed runs are not JASS-flow evidence.
- War3Net/wc3libs provide script/format references; the installed mission and
  the hash-matched running retail binary are the authority for this finding.
- This establishes one cause of out-of-order campaign flow. It does not certify
  the village encounters, Watcher prison rescues, harbor/ship objectives, or
  victory sequence end to end. The optional Wildkin cleanup uses the same
  affected enumeration pattern, but those later gameplay sequences need their
  own controlled comparisons.

See also [JASS groups](jass-groups.md), [trigger dispatch](trigger-events.md),
and the [campaign audit](map-audit.md).

See [portrait animation and destructable hover parity](portrait-and-hover-parity.md)
for the retail comparison and renderer/snapshot regressions.
