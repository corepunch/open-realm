<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/FOOT-03.2/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **FOOT-03.2**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/FOOT-03.2/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-FOOT-03.2.json` -> [`FOOT-03.2-expected.json`](../../../../../tools/ghidra/fixtures/research/FOOT-03.2-expected.json) (uncompressed sha256 `ef379cbfc9441c0b9f27d7a2a3a63cea0c550939c12561bea08102586b190df1`, 370665 bytes)

# FOOT-03.2 handoff — two eligible categories in one fine cell, removed in both orders (research, not a closure)

**Status.** Established at two levels. (1) **Original-code oracle over real producers**: unit Footman object
(`14cf20` + `15ee0a` + `1c53f0` + `05c7e0` + `160590`) and an LTcr-like widget (`064460` → `14cf70`, raster
`22e9c0`/`652b40`) share cell C=(6,6); 16 scenarios = 2 insertion orders × 2 unit removals (`14dae0` retire / `160590`
move-away) × 2 widget removals (`063b40` collection retire / `22e9c0`+`650a70` unraster) × 2 removal orders, each with
all consumers, counters, dirty-cell (`14df20`) and full (`14dfc0`) compaction; deterministic repeat identical.
(2) **Live retail** (read-only observer, isolated env): Footman inside an `LTcr` crate footprint at two sites, unit-first
and crate-first removal; complete chains/counters at every phase, 164 live 1489a0/148e90 calls on C equal the FOOT-03.1
model, JASS endpoint probes (ground SetUnitPosition, item admission, flyer) show the category split; observer-free
control reproduces all 60 JASS markers exactly; a repeat observed capture is identical (markers, chain words, counts, all 164 consumer calls). Not established: live segment
sampler / collector calls on C (no movement through C in the probe), live full-sweep compaction timing.

## Functions
All ABIs as in BASE-02.2 / FOOT-03.1 handoffs (asm-verified); additionally executed here: `6f160590`
Mover_UpdateFineOccupancyBounds (thiscall ECX mover, [4] radius*, [8] position*, RET 8), `6f063b40`, `6f14dae0`,
`6f14df20`/`6f14dfc0` (thiscall ECX map, RET plain), `6f22e9c0` (texture, centre*, orientation, callback, collection; RET 14).

## Behaviour (oracle; `expected-FOOT-03.2.json` sha256 `ef379cbfc9441c0b9f27d7a2a3a63cea0c550939c12561bea08102586b190df1`)

A = unit (`010000ca`, hfoo radius 0.96875 at (7,7) ⇒ cells 6..7²), B = regions `c2/10/08` (3×3 texture `da` ⇒ cells 5..7²).
Ground = `02000002`, item = `10000010`, build = `08000008`; fine = 1489a0, seg = 149440 full, ep = 1492b0 mode1,
pq = 04df50 (no bridge / A's bridge), hier = 15d360 ground class of block (6..7)², coll = 148ad0 tokens.

| State at C | fine g m0/m1 | build | item | seg | ep | pq (none/A) | hier g | coll | union |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A only | 0/0 | 0 | 1 | 0 | 0 | 1/0 | 00 | [A] | `ca` |
| A+B (A first) | 0/0 | 0 | 0 | 0 | 0 | 1/1 | **01** | [null(B),A] | `da` |
| A+B (B first) | 0/0 | 0 | 0 | 0 | 0 | 1/1 | 01 | [A,null] | `da` |
| B only (A retired or moved) | 0/0 | 0 | 0 | 0 | 0 | 1/1 | 01 | [null] | `da` |
| A only (B retired or unrastered) | 0/0 | 0 | 1 | 0 | 0 | 1/0 | **00** | [A] | `ca` |
| neither | 1/1 | 1 | 1 | 1 | 1 | 0/0 | 00 | [] | `0` |

Surviving reference counts (object `+3c` low24, map `+b0`; whole 16×16 map):

| Path | after both inserted | after A retire | after A move | after B retire | after B unraster | dirty compaction | full compaction |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A refs | 4 | 8 (4 ins+4 rem), +38=-1 | 12 (4+4+4 new) | unchanged | unchanged | 0 (retired) / 4 (moved) | 0 / 4 |
| each B region refs | 9 | 9 | 9 | 9, +38=-1, **no records, cells not dirtied** | 18 (9+9 removal) | 5 (retire: only 4 dirty cells cleaned) / 0 (unraster) | 0 |
| map `+b0` | 31 | 35 | 39 | +0 | +27 | 15/19 (B retired) or 0/4 (B unrastered) | 0/4 |

Removal order does not change any consumer result or final count; insertion order changes only chain/collector order.
Consumers drop a retired static region immediately (+38=-1), long before its records are reclaimed.

## Live capture (RS-FOOT-03.2-overlap.w3m; site 0 C=(21,21) unit removed first; site 1 C=(41,21) crate first)

| Phase (site) | chain at C (head first; kind, +34, state, +3c) | `+b0` | ground probe | item probe | flyer |
| --- | --- | --- | --- | --- | --- |
| crate (0/1) | 1 `01000008` 16, 1 `01000010` 16, 1 `010000c2` 16 (+40 `10000000`) | 51 / 84 | displaced (656,624)/(1296,624) | displaced, same | exact (688,688)/(1328,688) |
| + Footman SetUnitX/Y | 1 `010000ca` refs 20→4 after next tick, then the 3 regions | 71 / 104 | displaced | displaced | exact |
| site0: RemoveUnit | 0+1 `000000ca` (active cleared, live, 8), regions | 59 | displaced (crate) | displaced | exact |
| site0: RemoveDestructable | regions `+38=ffffffff`, 16 each, no removal records | 53 | **exact (688,688)** | exact | exact |
| site1: RemoveDestructable | 1 `010000ca` 4, dead regions | 90 | **displaced (1328,656)** (unit) | **exact (1328,688)** | exact |
| site1: RemoveUnit | 0+1 `000000ca` 8 | 79 | exact | exact | exact |

Live hierarchy: `148e90` on C during crate creation returns 0/0/0/1 for lanes 06/80/40/04 and `15d0e0` writes
`40000000→50000000→54000000` (ground/amph/float all-blocked, flight clear) for the four 2×2 blocks of the crate;
on RemoveDestructable all lanes return 1 and words 0 — at site 1 **while the Footman still occupies C**.
Live survival: 33 dead region records in 11 site-0 cells (27 in 9 site-1 cells) persist to the end of the run
(≈3.5 s; dirty compaction only cleans cells dirtied by unit/probe removals). Registry globals `6fd68610`==`6fd6860c`
(validates the oracle fixture alias). Item probe objects carry `+34 01000018` (`00000018` after RemoveItem), payload tag
`60706f73`.

## Reproducer (repository root; live parts under the lock)
```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_FOOT-03.2_two_categories.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --report /GitHub/wc3-analysis/reports/pathfinding-1.27/research/FOOT-03.2/oracle-two-categories.json \
  --expected /GitHub/wc3-analysis/reports/pathfinding-1.27/research/FOOT-03.2/expected-FOOT-03.2.json
python3 tools/frida/research/foot032_make_map.py --base /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/Human02Interlude-original.w3m \
  --tool /run/media/lofcz/ssd_external/GitHub/open-realm/build/bin/mpqtool \
  --output /GitHub/wc3-analysis/reports/pathfinding-1.27/research/FOOT-03.2/maps/RS-FOOT-03.2-overlap.w3m
cp -n /GitHub/wc3-analysis/reports/pathfinding-1.27/research/FOOT-03.2/maps/RS-FOOT-03.2-overlap.w3m /run/media/lofcz/ssd_external/Games/w3-research/Maps/
for MODE in observe control; do
flock /GitHub/wc3-analysis/reports/pathfinding-1.27/research/_env/live.lock /home/lofcz/.local/share/uv/tools/frida-tools/bin/python \
  tools/frida/research/foot032_capture.py --data /run/media/lofcz/ssd_external/Games/w3-research --map 'Maps\RS-FOOT-03.2-overlap.w3m' \
  --mode $MODE --remote 127.0.0.1:27048 --x11-display :97 --seconds 200 --continue-at 60 \
  --output /GitHub/wc3-analysis/reports/pathfinding-1.27/research/FOOT-03.2/captures/$MODE-NEW.jsonl; done
python3 tools/frida/research/foot032_analyze.py --capture .../captures/observe-first.jsonl \
  --compare-preload .../captures/control-first-preload.txt --compare-preload .../captures/observe-repeat-preload.txt \
  --report /GitHub/wc3-analysis/reports/pathfinding-1.27/research/FOOT-03.2/live-analysis.json
```

## Provenance
DLL `d51e5680…d8236`; base map `Human02Interlude-original.w3m` `199683be…2104`; map `RS-FOOT-03.2-overlap.w3m`
`c2b4c8af…646e` (members: w3e `5ed7e2bb…`, wpm `166ff49c…`, doo `16d74745…`, Units `ce9dc954…`, j `07ff8f50…`).
Sources: probe `1fbffc44…189e`, builder `b739cae0…2992`, observer `5199678a…7236`, controller `35cab3a6…1385`
(all embedded in the capture metadata), analyzer `5011e264…8c47`, oracle `eb746c6d…f107`, rig `1a333f72…ca61`,
harness copy `f4905fd7…8fd3`. Oracle report `oracle-two-categories.json` `1d68e098…85aa`. Frida 17.18.0, display :97,
server 27048, pid 1460 (observe-first).

## Captures
| File | Mode | Status | sha256 |
| --- | --- | --- | --- |
| `captures/observe-first.jsonl` | observe | complete (60 markers, trace-end, 16 windows) | `57d74db4…bcd8` |
| `captures/observe-first-preload.txt` | JASS output | complete | `913ddd5c…e0fa` |
| `captures/control-first.jsonl` / `-preload.txt` | control (no attach) | complete, 60/60 markers equal | `6f33a93c…a4ea` / `65a1d217…e042` |
| `captures/control-first-previous-preload.txt` | preserved prior file (= observe-first output) | n/a | `913ddd5c…e0fa` |
| `captures/observe-repeat.jsonl` / `-preload.txt` | observe repeat | complete; 60/60 markers equal; all phase words/counts and all 164 C-cell consumer calls identical to first | `a470f0c2…382a` / `3e5575fc…ba92` |
| `captures/observe-repeat-previous-preload.txt` | preserved prior file (control output) | n/a | — |
| `live-analysis-first.json` / `live-analysis-repeat.json` | analyzer reports (model vs live, marker comparisons) | 0 mismatches each | `a3b970b9…298f` / `39137387…40e8` |

## Observer controls
Observer-free control and observed repeat equal all 60 `FOOT032` markers (positions to 3 decimals); 1 control, 2 observed runs. Whole preload files differ only in the
engine's random music track (`Human1.mp3` vs `Human2.mp3`) and `PreloadEnd(20.0)` vs `(20.4)` — recorded, not markers.
The observer only reads memory; hooks are entry/exit on 231df0, 1489a0, 148e90, 148ad0, 1492b0, 149320, 15d0e0.

## Exclusions
Live segment-sampler/collector calls on C during movement: FINE-01.6/ROUTE (no moving mover crossed C). Compaction
scheduling (`14e180` sampled driver, full-sweep timing): MAP-05.x. Building / under-construction overlap: E2E-02.2.

## Mismatches preserved
- None between oracle, model and live. Unit `+3c`=20 at end-insert-unit (creation at stage + two axis writes) is lazy
  history, compacted to 4 by the next tick; not a count error.

## Proposed integration
No new mapping rows beyond BASE-02.2/FOOT-03.1. `proposed-docs-FOOT-03.2.md`. Suggested failing engine regressions:
1. Footman + crate in one cell: remove the Footman ⇒ ground still blocked, item still blocked; remove the crate ⇒
   ground SetUnitPosition lands exactly at the cell centre (live 688,688).
2. Crate removed first ⇒ ground blocked by the Footman (live displaced to 1328,656), item admission exact (1328,688),
   hierarchy ground lane clear immediately although the Footman remains.
3. Retired crate regions stop blocking immediately although their records survive (counts above); retained records
   must not resurrect occupancy after unrelated cell edits.
