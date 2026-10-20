# Submerge passive availability (Morph372)

See [separation ownership](retail-pathfinding-separation.md),
[ability implementation](ability-implementation.md), and [save/load](save-load.md).
The reference is Warcraft III **1.27.1.7085** `game.dll`, SHA256
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`.

## Ownership and inverse

`Asb1`, `Asb2`, and `Asb3` suspend ordinary passive execution when forward
Submerge completes or an ability attaches to its initially submerged type.
Acquisition precedes hidden-state publication. An existing `Apiv` loses its fade
request; a late attachment inherits the aggregate and remains suspended.
`unsubmerge` removes the Submerge visibility source immediately, but retains
passive suspension throughout the authored reverse hold. Reverse completion
releases that ability's contribution and restarts `Apiv`'s authored fade.

`submerge_ability` uses the existing `S_SetMorphAbilityDisables` owner and
`morph_owned` flag. The shared helper preserves Morph, Move, Attack, Hero and
Inventory exceptions. Other suspension contributions remain independent.
Removal completes a pending forward transition before releasing its alternate
form owner. That completion acquires availability even though
`G_ActorRemoveSkill` has already removed membership; membership only controls
hidden-state publication. Skipping acquisition during removal would skip the
passive suspend/resume pair and retain its old fade state.

The three concrete Submerge procedures share `submerge_orders`.
`S_UnitOrderDisabled` accepts the attached owner's counters when its procedure
matches the resolved order owner **or** both use the same nonnull order table.
Resolving every `unsubmerge` to the first registered procedure rejected the
Royal Guard and Snap Dragon inverses once the aggregate became nonzero.
Execution and order counters still decide whether the order is available.

`morph_owned`, per-ability counters, `ability_exclusive_depth`, and the morph
request already use the normal serializer; this port adds no saved field.
The two `wc3_unit.morph372*` tests cover both `DataA1` and `Data11`, all three
families, existing/late `Apiv`, initially alternate attachment, exact reverse
completion, and detach while held, forward pending, reverse pending, or initially
alternate. Their save checks call `WriteGame`/`ReadGame` **within one process**
while hidden or a transition is live. They verify restored ownership and
subsequent deadlines; they do not establish fresh-process restoration.

## Failing-first evidence

Archives are under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-01.2/morph372/`.

| Run | Scope and result before correction |
|---|---|
| `root-red211-{classic,tft}.log` | Original 36 lifecycle cases: 180 failures of 1,356 assertions per edition; missing passive suspension/cancelled fade, including reload and reverse hold. |
| `root-detach-red211-{classic,tft}.log` | Original 24 detach cases: 24 failures of 936 assertions per edition; 12 missing held-passive suspensions and 12 uncancelled held-form fade requests. Pending-forward detach and other inverse checks passed. |
| `root-first-green211-classic-0.log` | First port: 300 failures of 3,896 assertions; shared-order admission rejected the two non-first Submerge families and produced inverse cascades. This was an unsuccessful run. |

`verified-private372-source.json` passes the strict verifier after isolated
production/test builds: **26 tests / 7,250 assertions per Classic and TFT**,
1,474 original instruction guards, three captures, five scenes, 311 public
markers, 449 active native events plus two literal pre-start exception rows,
and 762 projected records. The 29 Python integrity checks also pass. The earlier
shared-workspace report is retained as `verified-shared211-first.json`;
source isolation and commit integration remain distinct checks. These failure
records remain retained independently of the green runs.

## Portable native contract

`retail-morph372-1.27.json` and its compressed input bundle retain
`observe2.jsonl`, independent `observe3.jsonl`, and observer-free `control1`
public output. The failed `observe1` launch remains in the research archive.
Each accepted public stream contains 311 identical markers across five scenes,
ending at tick299/scene4. The observer player (`Player(1)`) inherits allied
shared vision from the base map. Its public `IsUnitInvisible` values stay false
and provide no positive enemy-visibility witness. The native invisibility count,
publication flags, fade-object state and ability-availability stream establish
the scoped state transitions. Morph374b adds a separate enemy-viewer control;
that does not retroactively change this capture's public evidence.
Each observed run has 449 active native events and 311 sequenced marker events. Source, map, controller binary and map-member pins
travel with the bundle; the verifier reconstructs `Morph372b.w3m` from the
embedded base map and checks the full manifest.

The projection preserves native sequence, clocks, stable identities, counters
and scalar words literally. Only `Unit+0x128` and `+0x12c` use a whole-capture
pointer bijection retaining aliases, nulls, sentinels and reuse. These offsets
come from native dereferences, not numerical classification: `66981e..669836`
loads `Unit128`, decrements the pointee's `+4` reference count, invokes its
virtual destructor and stores the `058120` modifier result. `669788` retains
`&Unit12c`; `669869` obtains a `058090` threshold-listener result, and
`669883..669897` performs the same pointee reference-count/destructor/store
sequence. `694b20` repeats both inverse paths. `Unit118/11c/120/124`, including
registry slot/generation, remain literal. The portable projection contract
requires these exact offset sets and 15 proof instruction sites. Its 1,474
static guards reuse complete functions from Submerge360 and Burrow366.

Both observed runs also contain **two reported pre-start system exceptions**
outside `game.dll`, before the active marker stream. Their complete rows remain
literal in the 762-record projection; changed, additional or removed exceptions
are rejected. The observer returns false from its exception handler. The later
native/public streams and completion footers agree; this establishes neither the
exceptions' cause nor OS-fault unreachability. The captures are not exception-free.

Run integrity checks with:

```sh
python3 -m unittest discover -s tests -p test_wc3_pathfinding_morph372.py
python3 tools/ghidra/verify_wc3_pathing_morph372.py \
  --binary /path/to/original/game.dll --report /new/path/morph372.json
```

The strict command verifies original instruction bytes, rebuilds the map, and
runs the listed engine patterns in Classic and TFT. A report path must be new.
