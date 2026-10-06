<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-04.1/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **MAP-04.1**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-04.1/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-MAP-04.1.json` -> [`MAP-04.1-expected.json`](../../../../../tools/ghidra/fixtures/research/MAP-04.1-expected.json) (uncompressed sha256 `4a95b59fdd95872f464a33fbd21eca0f6eed60ee78f286b844aa83049b6475b0`, 137359 bytes)

# MAP-04.1 handoff — nested self/target exclusions over overlapping objects and terrain

**Status.** Established, not closed:
- Original-code emulation (Unicorn, unmodified `game.dll`, no stubs), 45 complete coarse requests (`166c30`) and 45 complete fine requests (`166e90`).
  - Fixture: two overlapping objects (self A, target B), an overlapping bystander C, and blocked terrain at five places.
  - Every hierarchy class byte (1,360 bytes) is snapshotted at each exclusion boundary.
  - Every fine cell in an 18×18 window is re-queried with the original `1489a0` under each counter state.
- Instruction-verified ABIs and restore order.
- Live retail observation in the isolated environment:
  - Counter and class restoration around every observed request in 3 complete captures (2 maps).
  - A three-deep nested exclusion on one object (`69a840 → 651590 → 05ca50 → 170080`).

Not established:
- A live public scene whose target rectangle contains *hierarchy-participating* overlapping objects. On the live maps the units do not contribute to hierarchy classes, so the coarse clear is a byte no-op except where a pending terrain edit exists (see MAP-04.2).
- Live counter values inside `169c50`/`169d60` group scopes. Only balanced call pairs were seen.

**The TODO wording "restore in reverse order" is only true for the fine counters.** The coarse scope restores in the *same* order as it excluded, and it restores by *rebuilding from fine cells*, not by putting back saved bytes.

## Functions (ABIs from assembly)

| VA | Role | ABI | Evidence |
| --- | --- | --- | --- |
| 6f166e90 | Path_RequestFineRoute (fine exclusion scope) | thiscall ECX path, [ebp+8] source*, [ebp+c] goal*, RET 8, EAX 0/1; SEH FuncInfo 6fc55c7c; SSE `ucomiss` endpoint test | 166f59 `mov esi,[ebx+a0]`, 166f66 `inc [esi+40]`, 166f69 `mov edi,[ebx+a4]`, 166f7d `inc [edi+40]`, 166fb9 call 148100, 167041 `dec [edi+40]`, 167048 `dec [esi+40]`, 167061 `ret 8`; oracle 45 cases |
| 6f166c30 | Path_RequestAcceleratedRoute (coarse exclusion scope) | thiscall ECX path, [ebp+8] source*, [ebp+c] goal*, [ebp+10] passthrough, RET 0xc, EAX 0/1; no SEH frame | 166d23..166d35 `mov eax,[ebx+a0]; push 1; add eax,1c; mov ecx,edi; push eax; call 15d360`, 166d3a..166d4c same for `[ebx+a4]`, 166d7b call 162cb0, 166dc5..166dd7 / 166ddc..166dee: **re-read** `[ebx+a0]`/`[ebx+a4]`, `push 0` |
| 6f15d360 | PathMaps_UpdateRectangle | thiscall ECX owner, stack rect*(min_y,min_x,max_y,max_x) or 0, mode (1 clear / 0 rebuild), RET 8 | 15d41d `ret 8`; oracle primitive replay equals in-wrapper snapshots (45/45) |
| 6f15cf80 | PathMaps_RebuildBase | stdcall-like (rect*, map, mode), `mov byte [eax+7],0` at 15d052, then 15d0e0 ×4 lanes only when mode 0 | asm 15d052..15d0a7 |
| 6f148e90 | PathFine_TestAcceleratorCell (base classification cell test) | thiscall ECX fine, x, y, RET 8 | Object counts only if +40 bit 10000000 set (148f4f) and (+40 & 8fffffff)==0 (148f71). The link walk stops after 49 links: `inc esi; cmp esi,32; jge` at 148f10, so the 50th and later links are ignored |
| 6f1489a0 | PathFine_TestOccupiedCell | thiscall ECX fine, x, y, RET 8, EAX 1 clear/0 blocked; writes fine+cc (target seen), +d0, obj+38 | Target identity is tested at 148a52 before the exclusion test at 148a67 |
| 6f148100 | PathFine_BuildRoute | thiscall ECX fine system, 7 stack args (table, source*, goal*, mask*, limit, radius*, target), RET 1c | 166f91..166fb9 pushes |
| 6f162cb0 | PathAcc_BuildRoute | thiscall ECX adaptive system (15afe0), 7 stack args (lane*2, table, source*, goal*, limit, size, passthrough), RET 1c | 166d51..166d7b |
| 6f058600 / 6f058740 | SpatialExclusionHolder_Acquire / _Release (renamed) | thiscall ECX holder, stack obj, RET 4 / fastcall ECX holder, RET | Called only from C++ unwind funclets (9e7530/9e7538 for 166e90 states 0/1) |
| 6f05bd30 | UnitMoverBridge_ToggleSpatialExclusion (renamed) | thiscall ECX bridge (+8/+c handle), stack on, RET 4, EAX = object | 05bd42 `mov eax,[eax+98]` (mover spatial object), 05bd4a inc / 05bd51 dec |
| 6f651590 | Unit_ToggleSpatialExclusion (renamed; vtable +d0) | thiscall ECX unit, stack on, RET 4 | 6515c2 `call [eax+b8]` → 05bd30, 6515d8 → 063d10 |
| 6f05ca50 | MoverBridge_StopWithRecovery | thiscall ECX bridge, 2 stack args, RET 8 | 05ca68 05bd30(1), 05ca86 call 171340, 05ca8f 05bd30(0) |
| 6f170080 | Mover_RecoverEmbeddedFinePoint | thiscall ECX mover (ebx), RET 0x14 | 170126 `mov esi,[ebx+98]`, 170133 inc, 1701d2 dec |

## Fields

| Struct+off | Meaning | Encoding | Writes | Reads | Evidence |
| --- | --- | --- | --- | --- | --- |
| spatial+1c..+28 | Fine rectangle | i32 min_y, min_x, max_y, max_x, half-open | 14e770 family | 166d2f, 166dd1 (re-read at restore), 059730 | asm + oracle (asymmetric B rect) |
| spatial+34 | Category flags | bit 01000000 = live; low 24 bits = category | 05c7b0 | 148a3d/148a80, 148f45/148f79 | asm |
| spatial+38 | Per-query visit stamp; -1 = retired | i32 | 148a47, 148f60 | 148a35, 148f3f | asm |
| spatial+40 | Occupancy/exclusion word | Low 28 bits + bit31 = exclusion counter (blocks only when `&8fffffff==0`); bit 10000000 = hierarchy-participating; 20000000/40000000 = moving/exemption | inc/dec 166f66/166f7d/167041/167048, 167c2a/167cbd, 166265/1662dd, 16ec34/16eca0, 16ef24/16ef61, 170133/1701d2, 05bd4a/05bd51, 063d2e/063d33, 16da72/16da79; bit28 set 14cf7e/14ead0 | 148a64 (fine query), 148f4f/148f63 (hierarchy test), 148b8a (step blocker collector) | asm + oracle + live |
| path+a0 / +a4 | Self / target spatial object | ptr | outside the scopes | 166f59/166f69 (fine, kept in esi/edi); 166d23/166d3a and again 166dc5/166ddc (coarse, re-read) | asm |
| hierarchy map+28 / +3c / +40 | 8-byte cell records / width / height | Class byte at record+7 = four 2-bit lanes; lane mask 06000006 is bits 7..6 (`0x80000000>>shift`): 0 clear, 1 all blocked, 2 mixed | 15d052 (clear), 15d0e0 (set) | searches | asm + oracle |
| tilemap+54..+60 | Clip bounds | min_y, min_x, max_y, max_x | map setup | 15d360 clip | asm |

## Behaviour (frozen in `expected-MAP-04.1.json`)

Fixture:
- 64×64 fine map; 32/16/8/4 hierarchy rebuilt with the original `15d360(0,0)`.
- Objects: A=(16,16,20,20), B=(18,19,23,22), C=(21,21,25,26) (min_y,min_x,max_y,max_x).
- Terrain:
  - (17,17) 0xff in A;
  - (19,18) 0xff in A∩B;
  - (21,21) 0x02 in B∩C;
  - (23,20) 0xff outside every object but inside B's rounded coverage;
  - (40,40) 0xff as a far control.
- Five role cases: self/target = A/B, B/A, A/A, A/–, –/B.
- Three +40 regimes:
  - dynamic: all 0;
  - hierarchy: all 10000000;
  - outer_scope: A=1, B=10000001, C=10000000.
- Three cell-link orders: ABC, BAC, CBA.

| Scope | Exact order | Restore semantics | Frozen consequence |
| --- | --- | --- | --- |
| Fine 166e90 | inc self → inc target → search (148100) → dec target → dec self | Reverse order; counters only, no cell/link writes | Counters at search: A/B dynamic = 1/1; A=A gives 2; outer_scope gives A=2, B=10000002, C unchanged. All 45 cases end with exactly the prior words |
| Coarse 166c30 | clear self → clear target → search (162cb0) → rebuild self → rebuild target | Same order; mode 0 recomputes from fine cells and objects | All 45 end byte-equal to the baseline. The self rebuild already restores target-covered cells (see below) |

Coarse A/B, dynamic regime. Entries are (level, x, y, before→after):
- after clear self: `0,8,8 AA→0; 0,9,9 AA→0; 0,10,10 80→0; 1,4,4 AA→0`.
- after clear target: adds `0,11,10 AA→0; 1,5,5 AA→0; 2,2,2 AA→0; 3,1,1 AA→0`.
- Search entry equals "after clear target" (8 cells).
- After rebuild self, only `0,11,10 =0` and `1,5,5 =80` still differ from baseline. Target-covered cells (9,9)=AA and (10,10)=80 are back *before* the target rebuild.

Same comparison in the other regimes:
- Hierarchy regime: 4 such early-restored cells ((9,9)=69, (9,10)=82, (10,9)=41, (10,10)=41).
- The order is irrelevant to the final bytes. Results are independent of link order (verified across all 3 orders).

Hierarchy objects:
- With bit 10000000 and counter 0, an object contributes to base classes: 32 nonzero bytes, versus 12 with terrain only.
- In outer_scope, B's counter of 1 hides it from the rebuild: 25 nonzero bytes.
- So any exclusion that is still held while a rebuild runs publishes the object as absent.

Fine per-cell effect (A/B, dynamic, `1489a0` value = clear | target_seen<<1):
- After self inc, 13 A-only cells go 0→1.
- Terrain (17,17) and (19,18) stay 0.
- Overlap cell (19,19):
  - with link order ABC (A first) it goes 0→2 after self inc, then →3 at search;
  - with BAC/CBA it goes 2→3.
  - So excluding self *un-hides target identity* where self's link precedes the target's.
- At search, 25 cells differ from before. B∩C cell (21,22) stays blocked by C, and (21,21) by ground terrain.
- After both decrements the window is identical to before.

### Live retail (isolated env, `MAP-04.2/captures`)

| Capture | Map | Result |
| --- | --- | --- |
| target-overlap-observe-2 | PathingRE-TargetOverlap81-261003 | Complete. 3 fine and 9 coarse requests; all restored; LIFO; 0 foreign writers inside request scopes. Target rect (31,30,34,33) is linked with 2 other objects whose +40 stays 0 at search. 300-tick JASS markers equal the reference capture `runtime/target-overlap81-repeat.jsonl` (different observer) |
| target-edit-observe-1 (and -2, see MAP-04.2) | RS-MAP-04.2-target-edit | Complete. Same counts. Nested ledger on the paused target unit's spatial object (0x10ef0290): 651590(1) → 05bd30(1) gives word 2 → 170080 entry word 2 (it raises to 3 internally) → leave 2 → 05bd30(0) gives 1 → 651590(0). 24 nested `stop_with_recovery > embedded_recovery` scopes, all LIFO |

## Reproducers (from the repository root)

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_map04_1_nested_exclusions.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --report /tmp/map04-1-report.json \
  --reference /GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-04.1/expected-MAP-04.1.json \
  --edit-reference /GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-04.2/expected-oracle-MAP-04.2.json
```

The live commands are in MAP-04.2/HANDOFF.md (shared captures).

## Provenance

- game.dll sha256 `d51e5680…d8236` (original and research copies are identical); Unicorn 2.1.4; Frida 17.18.0.
- `verify_map04_1_nested_exclusions.py`:
  - Runs 1 and 2 used `f751e120…ac17`.
  - The final script `75160050…eefc` adds the MAP-04.2 exit cases; run 3 (`oracle-report-run3-final-script.json`) still produces the same MAP-04.1 payload.
- `expected-MAP-04.1.json` sha256 **`4a95b59fdd95872f464a33fbd21eca0f6eed60ee78f286b844aa83049b6475b0`**.
- Runs 1, 2 and 3 produced the identical MAP-04.1 payload.
- Observer-free control (the same 45 requests with no hooks installed) equals the observed finals in 45/45 cases (`observer_free_control_equal`).

## Explicit exclusions

- Edit during request, pending publication and exit inventory: MAP-04.2.
- Group-member counters inside 169c50/169d60 (only pairs observed): proposed **MAP-04.3** (text only), which would capture member +40 words through a 12-member `16bcf0`.
- The 49-link cap in 148e90 is recorded but not exercised: FOOT-03/MAP-05.
- Stamp wrap of tilemap+b4 to -1 (an object would read as retired): MAP-05.2 family.
- Fine target identity chronology: FINE-01.6.
- Nonzero world origin / non-dyadic maps: NUM-02.7.

## Mismatches preserved

1. The TODO says "restore in reverse order". Retail coarse restores in the same order and by rebuild. The intermediate state after `rebuild self` differs from a saved-byte reverse model in 18/45 cases. Final bytes are equal.
2. Capture `target-overlap-observe-1` failed: Frida `TypeError: no setter for property` because `this.depth` is reserved. It is kept as a failed capture and is not evidence.
3. Engine (code read only, not run): `g_world.c` `move_acc_object_rectangle` excludes only `move_has_dynamic_occupancy` objects. Retail excludes any nonnull path+a0/+a4. Not checked for flying/structure targets.
4. My first Ghidra plate on 05bd30 said "spatial object(+5c)". Assembly 05bd42 shows +98. A correction line was appended (see `MAP-04.2/ghidra-writes.jsonl`).

## Proposed integration

See `mapping-rows-MAP-04.1.txt` and `proposed-docs-MAP-04.1.md`. The Ghidra writes are logged in `../MAP-04.2/ghidra-writes.jsonl`.

## Suggested failing engine regressions

1. **Coarse restore = rebuild.** Build hierarchy H0. Edit fine terrain under the target's rounded coverage without publishing. Run one owned adaptive request with that target. Expect H1 ≠ H0 exactly at the coverage cells, and equal to a fresh full publication there; elsewhere expect H0. An engine that restores saved bytes fails this.
2. **Search-time coverage.** With A/B as in the fixture, the hierarchy seen by the adaptive search must have base cells x8..11 × y8..11 (union of the inclusive floor(min/2)..floor(max/2) boxes) cleared in all lanes, including terrain cell (23,20)'s base (11,10). After the request, the bytes are back to baseline.
3. **Fine nesting.** Self A and target B overlap; bystander C overlaps B. During the fine search, A and B are passable, C still blocks (21,22), terrain still blocks (17,17)/(19,18)/(21,21). After the request, A and B are blocking again. With self==target the counter rises by 2 and returns. With an outer exclusion (A already excluded), the request leaves A excluded.
4. **Same-object stop recovery.** A paused unit receiving Stop runs 651590(1) → 05bd30(1) → 170080. Its own footprint is ignored during recovery placement and the counter returns to 0 afterwards (live word sequence 1/2/(3)/2/1/0).
