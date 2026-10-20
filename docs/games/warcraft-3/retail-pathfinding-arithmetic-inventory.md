# Retail arithmetic and conversion inventory (NUM-01.2)

NUM-01.2 is complete as an inventory: **114 helper/family rows** cover a closed
universe of **11,671 original sites** (scalar library calls, imported CRT math
and parse calls, the static float-to-int64 runtime, inline conversion/compare
families and all 2,005 CRT initializer words), with **1,486 explicit exclusions**
and **11,760 instruction guards** re-checked against the DLL. 80 rows are covered
by existing NUM evidence, 32 partial and 2 uncovered; every unresolved row is
owned by one existing open leaf and no TODO is added. Closing the inventory does
not resolve those rows.

The [inventory](../../../tools/ghidra/fixtures/retail-pathfinding-arithmetic-inventory-1.27.json)
uses the shared closed-site schema:

```sh
python3 tools/ghidra/audit_wc3_pathing_inventory.py \
  tools/ghidra/fixtures/retail-pathfinding-arithmetic-inventory-1.27.json \
  --binary /run/media/lofcz/ssd_external/Games/w3-research2/game.dll
```

It also records `residual_dispositions` for every "remains 01.2" clause in the
closed NUM-01.x leaves and the reassignment of the three corpus exclusions
NUM-01.2 owned. The integer-overflow fault lifetime observed by the unintegrated
Fault353 research (retail terminates at the unguarded VM `IDIV` at `91c0a2`;
the engine raises a recoverable error) and the formatter's native-buffer overflow
are E2E-05.2 rows. Scripts and cached Ghidra responses are archived under
`/GitHub/wc3-analysis/runtime/claude-num012/`.

## Scope

- **Pathing closure.** This is the direct-call closure (Ghidra `get_function_callees`)
  from 553 MapPathfinding.java roots whose names start with Path/Mover/Move*/UnitMover/
  SpatialMap/SpatialObject/Separate/CAbilityMove/Waygate/Order*/TaskPoint/Scope342.
  It contains 4,916 functions. 915 of them are reachable only through four presentation
  subtrees: CGameUI 367710, selection-group sync 291240, Audio_PlayLabel 34b150 and the
  image decoder 8e9660. Direct calls only, so virtual and indirect roots stay with BASE-03.1.
- **Software scalar library** (06f9c0..0715c0, plus 1d4c80, the trig thunks and Pow/log/exp).
  Every direct reference anywhere in the program is a row site, the same scope as Basic352.
- **Imported CRT math and parse functions, and the static int64 conversion runtime.**
  Sites in the pathing closure are row sites. All other sites are excluded, each with a reason.
- **Inline instruction families.** These come from the Ghidra disassembly of all 4,916
  closure functions (267,081 instructions).
- **CRT initializers.** All 2,005 nonzero words of the initterm table (6fa7cd44..6fa7ecb0).

## Counts

Status: 80 covered, 32 partial, 2 uncovered.

| Owner | Rows | What it owns |
| --- | --- | --- |
| E2E-06.1 | 14 | Gaussian dice; FP-environment reset; MSVCR120 libm sqrt/sin/cos/pow/tan/asin/atan2/ceil sites; static float→int64 conversion family; hardware-SSE and software composite initializers; x87 arithmetic sites |
| BASE-03.1 | 12 | Forwarding thunks with no references; remaining inline CVT/SSE/DIV/FILD sites; inline fine↔world exponent-scaling and underflow-guard sites |
| E2E-05.2 | 2 | Overflow lifetime of the formatter's native buffer; the VM's INT_MIN/-1 IDIV fault lifetime (Fault353 is research only) |
| NUM-01.12 | 1 | Math_DistanceSquaredSourceMinusPoint 070850 operand aliases |
| MOVE-01.1 | 1 | MoveBridge_NormalizeAngle 062930 (only sampled, turn-parameter setters) |
| TARGET-01.2 | 1 | MoveBridge_GetPredictedEdgeDistance 05ae70 (Follow approach range) |
| ORDER-01.13 | 1 | Cargo_QueryBoardingDistanceSquared 5a6f30 |
| BASE-02.3 | 1 | CRT atoi/atol/strto*/sscanf sites inside authored/config producers |
| NUM-03.2 | 1 | Inline COMISS/UCOMISS signed-zero and unordered (NaN) branches |

BASE-01.4 and BASE-03.2 are closed and own nothing. The two initializer rows that
previously pointed at BASE-03.2 now point at E2E-06.1.

## Newly discovered (not in existing NUM evidence)

1. **Math_DistanceSquaredSourceMinusPoint 070850.** This is a standalone software helper
   with 34 references, 16 of them from closure callers. It is not among the Basic352 41
   roots, Trig351 or Power349, so there has been no CFG operand or alias audit (owner NUM-01.12).
2. **PathRandom_RollDiceSum, dice>16 Gaussian branch (1ec180 → 1d12e0 → 1d11c0 → 1d5280).**
   It draws twice from PathRandom_Next and computes a custom table log2 that returns
   `_HUGE` for inputs <=1e-307. It calls the hardware libm `libm_sse2_sqrt/sin/cos`, and it
   caches the spare deviate in the process-global 6fd68634, which 1cc920 and 1cc950 share.
   The engine has no implementation of it (uncovered, E2E-06.1).
3. **FP environment reset 092f70.** It calls `_clearfp` and then `_control87(0x9001f,0xfffff)`.
   It is the only FP control producer, and it is reached on the path
   PathFootprint_CreateFromImage → image load. The engine relies on the host default.
4. **There is no `_ftol` import.** The x86 "ftol" class is a statically linked conversion
   family at 78ea10..78edaf (223 + 54 sites). Out-of-range or NaN input returns
   8000000000000000 and is reported via `_except1`. The pathing consumer is
   NetScheduler_AdvanceHostTime 32d7d9, on the branch where the replay factor is not one.
5. **170 hardware-SSE CRT initializers with no Ghidra functions.** They compute 1/tau →
   3e22f983 (34 of them) and 1/(pi/180) → 42652ee1 (14) with round-to-nearest, not the
   software truncating Divide. The other 122 are exact products. Five outputs have direct
   readers inside the closure (terrain construction and presentation).
6. **89 unnamed software composite initializers.** 67 were evaluated statically with the
   repo's integer model. Four outputs read by pathing code have no cited evidence:
   6fd6bf58=0.5 (Attack_BeginSpeedCapExemption), 6fd6fb3c=0.5 (Move_HandlePointTask), and
   6fd71724/6fd7181c=0.2 (C*Line_StartPointPath).
7. **Inline fine↔world ×32/÷32 exponent arithmetic.** In the closure: 27 AND 0x2800000
   sites, 78 ADD/LEA 0xfd800000 sites and 79 underflow-guard sites. The engine has no
   wc3_math.h primitive for this. The conversion is copied inline in s_move.c,
   wc3_pathing_arrival.h and wc3_ai_regions.h.

## Engine residuals (recorded, not owned rows)

- **s_move.c steering heuristics.** These use host ceilf/sqrtf/cosf/sinf and 0.01f
  tolerances (E2E-05.1).
- **jdo.c:262.** The INT_MIN/-1 recoverable error differs from the retail process
  termination that Fault353 observed (E2E-05.2).
- **g_support.c and g_model.c.** These use host libm against MSVCR120's `*_precise` (E2E-06.1).

## Exclusion-owner reassignment

The three exclusion rows that NUM-01.2 owned are reassigned in
`inventory.json:exclusion_owner_reassignment`:

- **oracle-arrival[0] and oracle-arrival-engine[0] → E2E-06.1.** Arrival distance and angle
  are now independent wc3_math ports, verified by NUM-01.17, NUM-01.20 and the 2342 engine cases.
- **oracle-basic352[0] → E2E-05.2.**

Each clause in the closed NUM-01.x texts that deferred work to "01.2" is resolved in
`residual_dispositions`.
