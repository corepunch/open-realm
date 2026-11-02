# Target ability ownership controls group Way Gate routing

Payoff149 integrates FORM-01.3's target policy into Move. Native `5fc640`
queries `48cb80(target,Adro,1,0,1,1)` before constructing the canonical request.
The lookup compares virtual class identity and its registered ancestry, includes
ordinary and item attachments, and does not compare only the authored rawcode.
Aliases of Cargo Drop therefore participate. Meat Drop and Tank Drop Pilot do
not: their retail classes differ even though the engine shares a procedure.

The flat ability registry declares `AB_MOVE_TARGET_NO_WARP` on Cargo Drop.
`S_UnitHasAbilityFlags` reads current authored, runtime, learned and item
membership without executing callbacks. Move samples the trait when it creates
each approach or persistent Follow group and retains bit `0x10` in that group.
Removing the target ability does not rewrite an existing request. A successor
task samples the target again. No saved layout or network contract changes.

Native `16ce10` passes `~(group.flags>>4)&1` to `167120` only when requesting a
new coarse route. Cached routes retain their policy. The engine carries this
through `movePathQuery_t.no_warp` into the adaptive search; published gates stay
available to subsequent ordinary requests. Local member routing is unchanged:
this evidence establishes the group coarse consumer specifically.

## Public retail witness

A new public JASS probe creates a ground Footman follower and target with one
active Way Gate. Four sequential lanes add no ability, Cargo Drop, Meat Drop or
Tank Drop Pilot to the target. Two clean read-only captures agree on all
**1,743 semantic records**, including **406 public timer markers**. An
observer-free run repeats every public marker exactly. The four native coarse
requests have warp arguments **1,0,1,1** and gate-marker presence **yes,no,yes,yes**.
Their retained group policies are `1801,1811,1801,1801`; native route lengths are
four, nine, four and four points.

The first capture reached completion but its wrapper rejected the missing
observer `finish` method. It remains archived as a failed attempt. Only the two
subsequent clean captures and their no-hook control certify the result.

Frozen semantic expectations and byte-preserved captures:

- `tools/ghidra/fixtures/retail-target-warp-live-1.27.json.gz`
- `tools/ghidra/fixtures/retail-target-warp-captures-1.27.json.gz`
- `tools/ghidra/research/verify_form013_warp_live.py`
- `tools/frida/research/form013_warp_probe.j` and `form013_warp_observer.js`

The map builder and bounded launcher are the existing `group032_make_map.py`
and `group032_capture.py`. Use the original Human02Interlude map, scenario
`formation_policy`, the new probe, and preload filename `payoff149-f13.txt`.
Captures use marker prefix `F13 ` and observer `form013_warp_observer.js` in
the isolated B/C environments. Assets and full logs remain under
`/GitHub/wc3-analysis/runtime/payoff149/`; accepted corpus inputs are copied to
`reports/pathfinding-1.27/runtime/target-warp-policy149/` under the same root.

Recheck the byte-preserved witness without launching retail:

```sh
python3 -m unittest discover -s tests -p test_wc3_pathfinding_target_warp.py
python3 tools/ghidra/run_wc3_pathfinding_corpus.py \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27 \
  --only live-target-warp-policy --output /tmp/target-warp-fresh
```

The corpus output directory must be fresh. For a new live run, build a uniquely
named map and launch it with the existing bounded B/C wrapper:

```sh
python3 tools/frida/research/group032_make_map.py \
  --base /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/Human02Interlude-original.w3m \
  --probe tools/frida/research/form013_warp_probe.j \
  --preload-output payoff149-f13.txt --task FORM-01.3 \
  --output /tmp/TargetWarpFresh.w3m
```

Copy that new map into the chosen isolated installation's `Maps` directory.
Run `group032_capture.py` through `research/_env/live.sh` with `--seconds 160`,
the marker/observer arguments above, and a fresh JSONL output. Run two
`--mode observe` captures and one `--mode control`; the verifier checks both
complete semantic repeats and the control's byte-hashed preload file.

## Engine regressions and limits

The corrected producer regression fails eight policy assertions before the
implementation. The gate consumer independently fails its disabled-warp
assertion. The initial producer fixture omitted `SVF_MONSTER`, so it rejected
before reaching Follow; its invalid dereference is retained in the red log.
A later fixture added the missing authored Tank Drop row. Neither mistake
caused a production behavior change.

Both editions now pass **98 engine test executions /6,920,546 assertions**:
the five-target approach/persistent/save test, live-membership test, existing
1,015-commit public Follow journey, all pathfinding tests and all Way Gate tests.
The controlled approach completion relocates the fixture then invokes actual
owner updates; it is not a new retail trajectory witness. Six Python checks
reject changed policy, warp arguments, incomplete captures and control drift.
The existing original/C multiple-gate oracle still matches512 requests.

Ghidra saves the lookup name and three consumer/producer notes. The saved
readback is `retail-target-warp-policy-ghidra-1.27.json`; corresponding rows
are mirrored in `MapPathfinding.java`.

FORM-01.3 remains open. Its static-only bypass, artillery and captain-target
producer compositions still require completion. The engine's stock flying
transport Follow remains on its legacy traversal under TARGET-02.1; these
ground-target policy witnesses do not establish its complete trajectory or
approach timing. The delivered spacing-bit reference and constant-pair evidence
remains available in the FORM-01.3 handoff. No new TODO children are added.
