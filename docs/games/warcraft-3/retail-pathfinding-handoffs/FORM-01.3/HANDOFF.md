<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/FORM-01.3/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **FORM-01.3**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/FORM-01.3/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-FORM-01.3.json` -> [`FORM-01.3-expected.json`](../../../../../tools/ghidra/fixtures/research/FORM-01.3-expected.json) (uncompressed sha256 `b5eb017a5d1d28577cbc0039529cefa5c906c68a031b78f278b39694ac0c564b`, 13676 bytes)

# FORM-01.3 handoff — canonical request / physical group policy flags: producers and one witness per reachable value

**Status.** **A** (assembly + whole-image rel32/absolute scan, `oracle/references-report.json`): the ten request+100
setters, all 18 call sites, every condition under which a public path writes a bit, and every consumer of bits 10 and
20. **O** (GROUP-03.2 initializer oracle): all bit-20 consumer pairs hold identical words. **L** (public producers,
read-only observer, each value in ≥2 bit-identical captures): group policy words `0`, `1000`, `1010`, `1200`, `1801`,
`1811`, `d00` at the `16bdb0` copy, with their producer chains and setter calls. **Unreachable:** bit `20`
(`16d7e0` has no reference; consumers inert). **Not re-captured:** UI Alt `2|4|8 = e` (Payoff109 live witness stands);
**static only:** captain target family `1c01`, `05c0e0` bypass producers, point-bridge bit10 (artillery line, non-unit).
The live consequence of bit 10 (warp edges disabled) is not yet witnessed.

## Functions

| VA | Role | ABI | Evidence |
|---|---|---|---|
| 6f16dc30/dc50/dcb0/dc70/dc90/d7e0/dde0/dd00/dc00/ddb0 | MoveRequest setters bits 1/2/4/8/10/20/100/200/400/800 | thiscall ECX request, [4] Boolean (`cmp [ebp+8],0`), `or`/`and dword [ecx+100]`, RET4 | A (bytes verified by scanner) |
| 6f16bf70 / 6f16da10 | set / clear bit 1000 (target bookkeeping, not a policy setter) | thiscall | A; L (excluded from policy rows) |
| 6f16bdb0 | MoveRequest_ActivateCandidateCohort: `group80 = request100` at 16be3d | existing ABI | A; L hook at 16b7b0 return 16be48 |
| 6f89caf0 | MoveRequest_SetFormationOptions (15 UI dispatcher calls) → 2,4,8 | existing | A; Payoff109 L |
| 6f89cd80 | CMoveReq_SetCanonicalFlag10Inverted (new name) | thiscall ECX wrapper, [4] value, RET4; `16dc90(value==0)`; unreferenced | A |
| 6f5fc640 | Move_BeginTargetTaskRequest: computes 05a5c0 stack arg7 (bit10) | existing | A; L 1010/1811 |
| 6f16ce10 | PathGroup_RequestRoute: `~(group80>>4)&1` → 167120 → 166c30 → 162cb0 → 164c30 `warp_mode` | existing | A |

## Bits (request+100 → group+80)

| Bit | Setter / call sites | Public condition | Consumer | Verdict |
|---|---|---|---|---|
| 1 | 16dc30 ← 05a633 (05a5c0 arg6≠0), 89cb43 (89cb30 ← 6693f0 data, 9d111f captain 0) | persistent target task | 16c390 completion gate | L `1801`,`1811` |
| 2,4,8 | 16dc50/16dcb0/16dc70 ← 89caf0 only | UI packet10 Alt | 16c250 classification bypass (2), 16b120 regroup status (4), 169b50 shared cap and 168447 moving-blocker yield (8) | L Payoff109 `e` |
| 10 | 16dc90 ← 05a65a (05a5c0 arg7==0), 05bb6a (05b970 arg7==0), 89cd99 (unreferenced) | 5fc640: target unit owns `Adro` (Goblin Zeppelin `nzep`); 05b970: CArtilleryLine `6cf8e4` only (CAbilityMove point tasks, CMissile/CMissileLine and captain pass 1; wrapper 05b940 unreferenced) | 16ce10 → group coarse search warp_mode 0 | L `1010`,`1811`; consequence A |
| 20 | 16d7e0: **no rel32, no absolute reference** | none | 16a5b0/16a6e0/16d270/16c0e0/16e4c0/16e4f0 pick c8/bc, cc/c0, d0/c4 pairs = `40b00001`/`40000000`/`40200000` both ways | unreachable, inert |
| 100 | 16dde0 ← 89cd73 (89cd60 ← 9d1116) | captain member (stack arg5≠0) | 16b120 near radius 16, 16c4f0 timeout | L `d00` |
| 200 | 16dd00 ← 05a677/05bb84 (range == `7f7fffff`), 05c1b0 (05c0e0 always), clears 89cb79/89cbc9/89cc7f/89ccd3 | attack chase 49a240 (range sentinel) | tests in 16a790, 16c630, 16c6d0, 16d990, 16de50, 16e430 (bypass) | L `1200`; 05c0e0 static |
| 400 | 16dc00 ← 89ccb3 (89cca0 ← 9d1131) | captain | 169680 extra target refresh | L `d00` |
| 800 | 16ddb0 ← 05a63c, 89cd53 | persistent target task, captain | 16b5c0 (GROUP-03.2) | L `1801`,`1811`,`d00` |

## Witnesses (group policy word at the 16bdb0 copy; all values repeat bit-identically)

| Word | Producer (return chain from 16bd83) | Setter calls since previous activation | First witness | Captures |
|---|---|---|---|---|
| `0` | CAbilityMove d016b point task (`5bb90←5ffdb4←5fdbc3`): JASS Move, patrol, individual attack-move, independent orders | — | GROUP-03.2 a tick 20 | a×2, b2×2, FORM-05.1 b×2 |
| `0` | d016c point task (`5fdbd6`) | — | a tick 520 | a×2 |
| `0` | shared point request (`89cd34←89ccfc←6692a7`): GroupPointOrder (3 members) / actual UI selection (6 members) | 16dd00(0) | a tick 165 / FORM-05.1 a tick 40 | a×2; FORM-05.1 a×2, d×2 |
| `0` | CaptainAI_PublishPointRequest (`5bb90←9d4580`) | — | captain tick 10 | captain×2 |
| `d00` | captain shared point packet (`89cd34…`) 12- and 1-member | 16dd00(0),16dde0(1),16dc30(0),16ddb0(1),16dc00(1) | captain tick 90 | captain×2 |
| `1000` | d0174 approach (`5a77b←5fc755←5ffa99←5fdb8c`), d016f (`5fdb38`), attack chase with real range (`49a2cb`) | — | a tick 10 / 290 / 355 | a×2, b2×2 |
| `1010` | d0174 approach to own Goblin Zeppelin | 16dc90(1) from 05a65f | b2 tick 10 | b2×2 |
| `1200` | attack chase 49a240 → 5fc640 (`49a2cb`), d016f with sentinel range | 16dd00(1) from 05a67c | a tick 300 | a×2, b2×2 |
| `1801` | d0173 persistent Follow (`5fdba1`), captain followers d0170 (`5fdb62`) | 16dc30(1),16ddb0(1) | a tick 10; captain tick 10 | a×2, b2×2, captain×2 |
| `1811` | d0173 persistent, target owns `Adro` | 16dc30(1),16ddb0(1),16dc90(1) | b2 tick 10 | b2×2 |
| `e` | UI Alt (89caf0) | 16dc50,16dcb0,16dc70 | Payoff109 b/d | existing fixture |

Note: the canonical request is one global object (`800a0` in every capture) reused by all producers; only setter calls
since its previous activation belong to a word. Bit `1000` (`16bf70`/`16da10`) is runtime target bookkeeping.

## Frozen expectations
`expected-FORM-01.3.json` sha256 `b5eb017a5d1d28577cbc0039529cefa5c906c68a031b78f278b39694ac0c564b` (bit verdicts, all
witnesses with capture hashes, flag-20 pairs, live spacing globals). Inputs: `analysis/policy-witnesses.json`
`49b0fc8890e599064834fff7ec694b8801260e065a68415736939c0a01b3e5fb`, `oracle/references-report.json`
`b47d47984a7d1bcd6682aae413fa93490197ec9c539d1178bb3a0a38ccbb4d62` (compare mode exits 0),
GROUP-03.2 `oracle/constants-report.json` `9d9a404084d74ff4f142926ec909e01c7ffd1d52a61170e08933a91076d51e7a`.

## Reproducer
```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research; G=$R/GROUP-03.2/captures; F=$R/FORM-05.1/captures
python3 tools/ghidra/research/verify_FORM-01.3_references.py --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --report /tmp/refs.json --expected $R/FORM-01.3/oracle/references-report.json
python3 tools/frida/research/form013_policy_table.py $G/a-observe-{1,2}.jsonl $G/b2-observe-{1,2}.jsonl $G/captain-gohome-observe-{1,2}.jsonl \
  $F/a-observe-{1,2}.jsonl $F/b-observe-{1,2}.jsonl $F/d-observe-{1,2}.jsonl --output /tmp/witnesses.json
python3 tools/frida/research/form013_expected.py --witnesses /tmp/witnesses.json --references /tmp/refs.json \
  --constants $R/GROUP-03.2/oracle/constants-report.json --globals-capture $G/a-observe-1.jsonl --output /tmp/expected.json
```
Live captures: GROUP-03.2 reproducer (maps a/b2, captain map) and FORM-05.1 (`form05_runs.sh a|b|d observe-N`).

## Provenance
game.dll `d51e5680…d8236`. Captures (sha256): GROUP-03.2 as listed in its handoff; FORM-05.1 a-observe-1/2
`c5f947d1…b331`/`eabe1772…1075`, b-observe-1/2 `aaf0930e…3843`/`c534aca1…066b`, d-observe-1/2 `361a4942…34e2`/
`e440db5f…1309`; maps FORM-05.1 a/b/d `a92b367b…e0cd`/`6d80cedb…631f`/`46e6917f…bb96`; observer `form05_observer.js`
`ee644a75…6ab5`. Tools: `form013_policy_table.py` `f197e376…787b`, `form013_expected.py` `cef49c67…08d5`,
`verify_FORM-01.3_references.py` `943f2396…7153`.

## Captures
No FORM-01.3-only launches; every witness above comes from completed, repeated GROUP-03.2 / FORM-05.1 captures (status
and failures are listed in those handoffs). Observer-free controls: GROUP-03.2 a-control-1 and FORM-05.1 controls.

## Exclusions
Bit-10 warp consequence and static-only producers → proposed FORM-01.3.1/01.3.2; target-speed semantics of 800 →
GROUP-03.2; Alt request lifetime under Shift queues → FORM-03.1 diagnostic (Payoff109); captain policies → GROUP-03.4.x.

## Mismatches preserved
* Engine doc: "Its setter `16d7e0` has no static code xrefs … This does not prove complete indirect unreachability." — the
  whole image (all sections) contains no rel32 and no absolute occurrence of `6f16d7e0`; group+80 has no other bit-20
  writer in the path module; consumers are value-identical → unreachable *and* inert (A+O+L globals).
* Engine doc "`16dc90` writes another independent bit10" — meaning recovered: group coarse search warp_mode = !bit10.
* Routes doc: "`6f05a5c0` enables it together with flag `0x800` when its seventh stack argument is nonzero" — it is the
  stack argument at `[ebp+1c]` (sixth of eight; RET 20); `[ebp+20]` (seventh) controls bit 10 (set when it is 0).
* The single global canonical request is reused by every producer; setter calls must be attributed per activation.

## Proposed integration (not applied)
`mapping-rows-FORM-01.3.txt` (7 rows mirroring `ghidra-writes.jsonl`: 6 appended comments, 1 rename+comment),
`types-FORM-01.3.json` (10 setter/wrapper prototypes; appended evidence for request+100 and group+80 — if
`types-GROUP-03.2.json` is merged first, append this text after its evidence), `proposed-docs-FORM-01.3.md`.
New IDs (text): FORM-01.3.1, FORM-01.3.2.

## Engine entry points
* `games/warcraft-3/game/skills/s_move.c` `move_start_follow_group` (`0x1000|(persistent?0x801:0)`): add bit `0x10`
  when the target unit owns `Adro` (approach `0x1010`, persistent `0x1811`).
* `move_group_route` / `G_UnitMoveGroupDestination` query: carry `warp = !(group->flags & 0x10)` into the group coarse
  search (Way Gate special edges) — consumer of bit 10.
* No engine work for bit 20 (keep a single spacing set); retain `move_group_captain_order` `0x1c01/0xd00` and UI `14`.

## Suggested failing engine regressions
1. Public JASS producers → physical group flags at admission: point Move, patrol, attack-move point, GroupPointOrder
   (3 units) and a 6-unit UI selection Move → `0`; Smart and target Move on an ally → approach `0x1000`, then persistent
   `0x1801`; target Move on an own Goblin Zeppelin → `0x1010` then `0x1811`; captain shared point → `0xd00`.
2. Bit 10 consumer: a group whose flags contain `0x10` must request its coarse route with warp disabled (Way Gate
   edges ignored), while the same order on a non-`Adro` target may use the gate (pairs with FORM-01.3.1 live witness).
3. Spacing: the formation layout must not change between flag-20 set/clear (constant words `40b00001/40000000/40200000`).
