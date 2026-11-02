<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-01.2/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **SEP-01.2**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-01.2/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-SEP-01.2.json` -> [`SEP-01.2-expected.json`](../../../../../tools/ghidra/fixtures/research/SEP-01.2-expected.json) (uncompressed sha256 `2d55fff8eedabc4db5ffaeb80519158af8676b1699efe5c495a96d72986de28d`, 33374 bytes)

# SEP-01.2 handoff — nonzero repulsion selectors and runtime category/rank/mask overrides

**Status.** Established: (a) the complete set of producers of the separation policy word (instruction-verified call/jmp
inventory of `6f1710e0`/`6f693d50` plus every `Unit_CanUseSeparation` input writer that refreshes), (b) the exact packed-word
rule including all aliasing (original-code oracle, 65,536 setter cases), (c) the behaviour of selector rows 5–15 (oracle +
live), (d) live enabled/disabled outcomes for every authored field value class and for five runtime producers (owner change,
channel begin/end, PauseUnit suspend/resume, removal, Amec), two identical observed runs + one observer-free JASS control.
Not established: the gameplay trigger of `CAbilityMechanicalCritter` slot+0x3e4 (the only Unit+60 bit0 setter; `UnitAddAbility('Amec')`
does **not** set it), live witnesses for type rebind (`6f670950`) and the `Unit_CancelAllOrders` path (`6f67e0e0`), save/load
(`6f16f6e0`) restore of selector/category/rank. "Mask" overrides of the endpoint check (mover `+a8`) are movement-type data owned by BASE-02.1.

## Functions (ABIs from assembly; asm excerpts in `static/asm-*.txt`, sha256 list `static/asm-sha256.txt`)

| VA | Role | ABI | Evidence |
|---|---|---|---|
| 6f693d50 Unit_RefreshSeparationConfiguration | sole gameplay policy producer | thiscall ECX=CUnit, no args, plain RET. Calls 66fc50 (EAX enable), 695130 (stores **AL only** at [ebp-8]), 695090 (movzx AX), 6951a0 (AL only at [ebp-4]); pushes 4 dwords to 05c9c0 with ECX=unit+0x164 | asm 6f693d50..d9b; live args e.g. `[1,0x0026f300,0,0x00200200]`: upper bytes are uninitialized stack, ignored |
| 6f05c9c0 MoverBridge_ConfigureSeparation (renamed) | handle→mover bridge | ECX=bridge, 4 stack args, RET 0x10; re-pushes args, `6f054530` (ECX/EDX, plain RET, args left) then `6f1710e0` consumes them | asm |
| 6f1710e0 Mover_ConfigureSeparation | destroy/alloc/pack/register | ECX=mover, (enable, selector, category, rank), RET 0x10. Old `+ac` destroyed via vtbl+0x10(0); `enable!=0` (any int) → alloc 16e6e0, sep+14=mover, setters 171040→1710c0→1711e0, insert 170820 (owner+0x514) | asm; live 301 configure rows |
| 6f171040 / 6f1710c0 / 6f1711e0 | selector / category / rank setters | ECX=sep, [esp+4], RET 4; AL<<16 mask fff0ffff / AX<<20 mask f00fffff / AL<<28 mask 0fffffff | oracle 65,536 cases |
| 6f66fc50 Unit_CanUseSeparation | enable | ECX=unit, plain RET, EAX=record+0x224 raw int iff: record found, `Unit+20&0x40000000==0`, `(int)Unit+198<=0`, `Unit+5c&0x100000==0`, `(int)Unit+54<=0`, `Unit+5c&0x200000==0`; else 0 | asm 66fc50..c92 |
| 6f66fca0/6f695130/6f6950c0/6f6951a0 UnitData_GetRepulse{Enabled,Param,Group,Prio} (renamed) | rawcode lookups | ECX=rawcode by value, plain RET, EAX=record+0x224/228/22c/230 or 0 | asm |
| 6f695090 Unit_GetSeparationCategory | category byte | ECX=unit, plain RET; `((BYTE[unit+60]&1 ? 15 : vslot+0xec()) &15)<<4 | (group&15)` | asm; owner 685da0 per SEP-01.4 |
| 6f48ef40 / 6f48bca0 Unit_Begin/EndChannelWork (renamed) | runtime disable | ECX=unit; `or/and Unit+20, 0x40000000` then **JMP** 693d50 | asm; live callers 4eec5c/4cc7d4 (CAbilityChannel) |
| 6f688d90 / 6f6785c0 Unit_Acquire/ReleaseSeparationSuppression | runtime disable | ECX=unit; `inc/dec Unit+198` then JMP 693d50 | asm; live 6946be/690656 (removal), 69c4ac/690689 (pause) |
| 6f69c490 / 6f69c580 Unit_OnSuspendBegin/EndEvent (renamed) | CUnit events 0xd0145/0xd0146 (handler 6f690490 = vtbl 6fb77eb0 slot+0xc) | ECX=unit, plain RET; refresh first, then acquire / release | asm; live PauseUnit true/false |
| 6f698ce0 Unit_SetPlayerOwner | owner producer | refresh at 698f88 after `Unit+58=new`; early exit when unchanged (698d13) | live caller 0x698f8d |
| 6f68a060 (CUnit vtbl slot+0x1ac) | creation | refresh at 68a4f5 | live 0x68a4fa ×72 |
| 6f670950 Unit_RebindTypeRetainingPublicIdentity | type rebind | refresh at 670d4d | static only |
| 6f67e0e0 | from Unit_CancelAllOrders when 5c&0x100000 | refresh at 67e0e3 | static only |
| 6f16eb20 Mover_Destroy / 6f16f6e0 (load) | destroy / restore | 1710e0(0,0,0,0) / 1710e0(stream enable if version≥0x1771,0,0,0) then sep vtbl+0x30 | asm only |

## Fields

| Field | Meaning | Width | Writers | Readers | Evidence |
|---|---|---|---|---|---|
| UnitData+0x224/228/22c/230 | repulse / repulseParam / repulseGroup / repulsePrio | raw int32 | loader 66ca55/66ca66/66ca77/66ca88 (66bf40) | getters above | asm |
| Sep+0x20 bits 16–19 / 20–27 / 28–31 | selector / category (owner nibble 24–27, group 20–23) / rank | packed | setters (only via 1710e0) | 1702f0 (`byte[+22]&15`), 170960 query, 16e830 | oracle + live |
| Unit+0x20 bit 0x40000000 | channel/work state (disables) | bit | 48ef40/48bca0 (+refresh); also 40b454, 4378a5, 437bd1, 532a90/99, 544028, 566e54, 6d0137, 3fae53 **without** refresh (receiver types not verified) | 66fc50 | asm, `static/flag-writers-inventory.txt` |
| Unit+0x198 | counted suppression | int | 688d90/6785c0 only (other `+198` writers 3fb90f/8216b4 are on non-unit classes, e.g. CAbilityHarvest) | 66fc50 | asm |
| Unit+0x5c 0x100000 / 0x200000 | suspend latch / scripted pause | bits | set 69c49e, clear 69c591 / set 69777c (Unit_BeginScriptedPause), clear 6977e0 | 66fc50 | asm + live f5c values |
| Unit+0x60 bit 0 (mask 0x1) | owner-nibble override → 15 | bit | 57f421 `or 1` (CAbilityMechanicalCritter slot+0x3e4), 58dfb6 `and ~1` (CBuffMechanicalCritter slot+0x33c); 5ada90/584720 unreferenced; **no refresh** | 695090 + 15 UI readers | asm + live (not set by UnitAddAbility) |

## Behaviour (frozen in `expected-SEP-01.2.json`, sha256 `2d55fff8eedabc4db5ffaeb80519158af8676b1699efe5c495a96d72986de28d`)

Packed word: `(prior&0xffff)|((repulseParam&15)<<16)|((repulseGroup&15)<<20)|((((Unit+60&1)?15:owner)&15)<<24)|((repulsePrio&15)<<28)`.

| Case (live, map RS-SEP-01.2-policy) | Words | Decision (6f16e830) | Moved |
|---|---|---|---|
| repulseParam 17 vs 1 | 00010000 both | eligible both | yes (row 1) |
| repulseGroup 17 vs 1 | 00100000 both | eligible | yes |
| repulsePrio 17 vs 1 | 10000000 both | eligible | yes |
| repulse=2 vs 1 | 00000000 | eligible | yes |
| selector 5 pair (zero row) | 00050000 | eligible every visit, **no cooldown** (117 vs 15 bodies) | never |
| selector 5 vs 0 | 00050000 / 0 | eligible both ways | only selector 0 moves |
| SetUnitOwner 1→0 (caller 698f8d) | 01000000→00000000 | category mismatch → eligible | yes after change |
| Channel begin/end (4eec5c/4cc7d4) | 0 → (sep destroyed) → 0 | partner sees `+ac null` ×15 | resumes after |
| PauseUnit / unpause (69c498,69c4ac / 69c588,690689) | 0 → none → none → 0 | `+ac null` | resumes after |
| UnitAddAbility('Amec') then SetUnitOwner(1) | 00000000 → **01000000** (Unit+60 stays 0) | mismatch vs owner-15 unit | no |
| RemoveUnit (6946be then 690656) | disable, re-enable (new sep), Mover_Destroy | — | — |

Oracle (`oracle/policy-oracle.json`): harness reproduces all 600 pair/105 tail words of the accepted SEP-02.4 fixture; rows 5–15 are all
zero words; with a zero row the pair slice never accumulates but **still draws once** at d<0.001; the tail zeroes the vector
(`[0,0x80000000]`→`[0,0]`) and leaves the packed word unchanged (no cooldown7).

## Reproducer (repository root)

```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_SEP-01.2_policy.py --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report $R/SEP-01.2/oracle/policy-oracle.json
python3 tools/frida/research/sep_research_map.py --data /run/media/lofcz/ssd_external/Games/w3-research --variant policy --name RS-SEP-01.2-policy --tool /run/media/lofcz/ssd_external/GitHub/open-realm/build/bin/mpqtool --keep $R/SEP-01.2/maps
tools/frida/research/sep_research_runs.sh observe:policy:RS-SEP-01.2-policy:75:SEP-01.2/captures/policy-observe-N:--pair-probes control:policy:RS-SEP-01.2-policy:75:SEP-01.2/captures/policy-control-N:
python3 tools/frida/research/SEP-01.3_policy_matrix.py --capture $R/SEP-01.2/captures/policy-observe-2 --map-json $R/SEP-01.2/maps/RS-SEP-01.2-policy.json --report $R/SEP-01.3/policy-matrix-observe-2.json
python3 tools/frida/research/sep_research_compare_preload.py $R/SEP-01.2/captures/policy-observe-2 $R/SEP-01.2/captures/policy-control-1 --out $R/SEP-01.2/control-compare-policy.json
python3 $R/SEP-01.2/make_expected.py
```
(`sep_research_runs.sh` wraps each launch in `flock .../_env/live.lock` with `--remote 127.0.0.1:27048 --x11-display :97 --data .../w3-research`.)

## Provenance

game.dll `d51e5680…d8236`, CRT `86e39b59…615f`. Map `RS-SEP-01.2-policy.w3m` `e09fc401f4d012344d85d062329aeced63da484d30e8180027cb87327e9f9ddb` (base `PathingRE-MovementBypasses86b-261003.w3m` `4b9ea0ba…6555`; build json `maps/RS-SEP-01.2-policy.json`); builder `0b8645f7…883d`; observer v1 `7778a95e…46be`, v2 `7eee4c82…b991` (v2 only adds the retry `current` field); controller `595ea808…477e995`; oracle module `ab7d52fb…86b5`, oracle script `69a4552c…5c81`; mpqtool `a14bab49…0225`. Seed: path-owner words before first separation visit `4273436052/209508436` (after 8 startup draws by `PlayerSetup_ResolveRaces` 6f1e9e25 from `2002874931/738765888`); policy cases draw nothing.

## Captures

| Capture | Status | capture / preload sha256 (prefix) |
|---|---|---|
| policy-observe-1 | **failed** (controller TypeError before attach; process killed) — not evidence | 52c698f2 / – |
| policy-observe-2 (obs v1) | complete, 6035 markers, 9477 updates | 756e0a11 / 712f9755 |
| policy-observe-3 (obs v2) | complete; all 36 cluster sequences equal to -2 (pointer-normalized words, visit counters) | b594870c / d1c79a26 |
| policy-control-1 (no attach) | complete; 6035/6035 markers identical | – / 68454f79 |

## Observer controls
Observer-free JASS run equals both observed runs marker-for-marker (`control-compare-policy.json`, `repeat-compare-policy.json`; positions R2SW 4 decimals every 0.1 s, orders, owners). Preload file hashes differ only outside PATHSEP lines.

## Exclusions
Endpoint/movement masks (`mover+a8`) → BASE-02.1; object categories → BASE-02.2; full pause/suspension semantics → engine Payoff101 scope; save/load of the word → SEP-02.5; morph/chaos rebind live witness → proposed **SEP-01.5**; Mechanical Critter trigger → proposed **SEP-01.6**.

## Mismatches preserved
* Ledger/Ghidra text "Unit+60 bit1 forces 15" (`retail-pathfinding-separation.md` l.377/700, plate 695090): the test is `test BYTE [unit+60],1` (bit 0, value 1). Appended clarification, nothing replaced.
* Ledger l.351–353 warns that invalid high selector bits could alter category bits: true for the setter alone, but in the only producer (1710e0) the category setter runs next and overwrites bits 20–27 (oracle), so no leak occurs.
* policy-observe-1 failure kept.

## Proposed integration
`mapping-rows-SEP-01.2.txt` (13 rows; 9 renames + 4 comment extensions, all already written to Ghidra and logged in `ghidra-writes.jsonl`, unsaved), `proposed-docs-SEP-01.2.md`.

## Suggested failing engine regressions
1. Authored `urpp=17,urpg=17,urpr=17` behaves exactly as `1/1/1` (word `0x10110000`), pairs with a `1/1/1` unit.
2. `urpp=5..15`: unit never moves, never enters cooldown, pushes a selector-0 neighbour; exact overlap still consumes one path-owner draw per visit.
3. `urpo=2` enables. `urpr` asymmetry: rank-1 unit never moves next to rank-0; rank-0 does.
4. Channel begin/end, PauseUnit, RemoveUnit sequences produce the word timeline in `expected-SEP-01.2.json` (`live_cases`).
5. Adding ability `Amec` does not change the category; SetUnitOwner to the same owner does not refresh.
