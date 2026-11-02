<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/NUM-04.5/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **NUM-04.5**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/NUM-04.5/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-NUM-04.5.json` -> [`NUM-04.5-expected.json`](../../../../../tools/ghidra/fixtures/research/NUM-04.5-expected.json) (uncompressed sha256 `36eac994ffd2912c698f542677564423435d8c024e79892270ff89debb6ac5be`, 106416 bytes)

> Research handoff for **NUM-04.5**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/NUM-04.5/`.
> Frozen expected results: `expected-NUM-04.5.json` (sha256 `36eac994ffd2912c698f542677564423435d8c024e79892270ff89debb6ac5be`, 106416 bytes).

# NUM-04.5 handoff — map/default seed production and startup order before the first pathfinding consumer

**Status.** Established (A + L, repeats and observer-free controls): the complete -loadfile seed producer chain, both
branches of the seed decision, and every consumer of the path-owner and 45-stream states from setup through the first
real movement and separation update. An injection-free model (seed rule + race preferences + generator port) reproduces
**every** recorded owner and stream word (0 mismatches) in three locked maps (8 / 4 / 0 race draws) and two unlocked runs
(time seeds 0x4e5d898, 0x4e68426, taken from the game's own setup record, not injected). Predictions for the two new maps
were written before capture (`predictions-before-capture.txt`) and matched. **Not established:** network-lobby/replay
seed transport beyond the local -loadfile record (assembly only, see exclusions); observer slots (payload78) in
race resolution (assembly only, not exercised). The 45-stream ownership/consumers are NUM-04.6.

**Correction to an existing claim.** Ghidra/MapPathfinding comment of `6f29e300`: "otherwise uses computed lobby seed".
In the -loadfile flow the unlocked seed is **not computed** in 29e300: `2a1170` → `96c8c0` parses the setup record and
29e300 uses record+0xc (`[ebp-0x3c50]`), which `2a46a0` stamps with `GetTickCount()`. The rol3 fold at `29e5c4` belongs to
the `29e459` path (no usable record) and never ran in 10 observed startups (no `setup-slot`/`setup-shuffle-seed` probe hit). The
locked word (or record seed) also reseeds the 45 CRandData streams (64f9a0 → 693710) — previously undocumented.

## Functions
| VA | Role | ABI (assembly) | Evidence |
|---|---|---|---|
| 6f2e2850 LoadFile_StartPendingFile | -loadfile start: preference 0x6a → 0x8000 in 6fce50b4; 0x69 → 0x1000000<<d | ECX frame, plain RET | asm; live `pref-bool` caller 6f2e29f7 in all runs |
| 6f71d580 WorldEditPref_GetBool | Storm reg key `WorldEdit`, row table 6fb810f8 (16 B), default +8 | ECX index, EDX forceDefault, EAX 0/1 | asm; live result 1 (absent), 0 (value 0) |
| 6f71d5d0 WorldEditPref_GetDword | same, raw value | ECX index, EDX forceDefault | asm |
| 6f2a46a0 GameLobby_StampSeedAndBroadcast | record seed [ECX+0x24] = GetTickCount(), then broadcast | ECX lobby | asm; live `setup-record-tick` (2a46ec) = lobby seed in 9/9 probed startups |
| 6f2a0c80 GameSetup_ApplyDescriptor | NetState create; mapflags \|= descriptor+0x34 | ECX descriptor, EAX 0/1 | asm; live 0x300000 → 0x2308000 |
| 6f96c8c0 GameSetupRecord_Deserialize | record {+4 count,+8 9-B slots,+0xc seed} from {u16 len, bytes} | ECX record, EDX blob, EAX bool | asm |
| 6f2a1170 (default name) | record usable → 29e300 jumps to 29e77c (record seed) | ECX record | asm; live path |
| 6f29e300 GameSetup_ConfigurePlayersAndSeed | `test [flags],0x8000` at 29ec0d: Seed(owner,0x77617233) / Seed(owner,record seed); ECX=same word → 64f9a0 | ECX mode (2 live) | asm; live probes 29ec0d |
| 6f64f9a0 RandData_ReseedForGame → 6f693710 | 45 streams from the same game seed (NUM-04.6) | ECX seed | asm + Unicorn oracle + live |
| 6f1e9dd0 PlayerSetup_ResolveRaces | 12 slots; pref&0x20 → one owner draw, race=(draw>>30)+1 | ECX game | asm; live 8/4/0 draws at 6f1e9e25 |
| 6f157610 PathOwner_Construct | boot seed 0x69707365 (04eec0→0509d0) | ECX owner, stack4 seed, RET4 | asm; live seed row caller 6f157644 |
| 6f201e30/6f201e70 Jass_GetRandomInt/Real | owner draws at 6f201e5e / 6f201ed6 | (NUM-04.4) | live |

## Seed producer chain (ordered, live -loadfile startup)
| # | Step | Words (mixed map) |
|---|---|---|
| 0 | boot: PathOwner_Construct Seed(0x69707365); audio global 6fd6a674 Seed(GetTickCount) (client-local, NUM-04.6) | owner 1768977253/2822785048 |
| 1 | config() runs twice (SetMapName probe) — no owner draw | owner unchanged |
| 2 | 2e2850: WorldEditPref_GetBool(0x6a) → 1 (absent) → LoadFile_GameFlags \|= 0x8000 | — |
| 3 | 2a46a0 ×2: record seed = GetTickCount (e.g. 81434655, then 81434830) | — |
| 4 | 2a0c80: descriptor+0x34 = 0x02008000 → mapflags 0x300000 → 0x2308000 | — |
| 5 | 29e300 (mode 2): flags 0x2308400 at 29ec0d; locked → Seed(owner,0x77617233) | 2002874931/738765888 |
| 6 | 64f9a0 → 693710(0x77617233): 45 streams (table in expected `war3_streams`) | — |
| 7 | (loading) 1e5a10 → 1e9dd0: one owner draw per RACE_PREF_RANDOM slot | mixed 4 draws → 4041395174/481053892 |
| 8 | JASS main (SetCameraBounds probe), probe init queries: 2×GetRandomInt + GetRandomReal | → 3946970562/277349488 |
| 9 | first PathOwner_Update visit (counter 1024) | unchanged |
| 10 | tick 1: ChooseRandomCreep/Item/ItemEx/NPBuilding → streams 32,35,35,34 only | owner unchanged |
| 11 | tick 2 unit creation: one stream-2 draw per attacking unit (CAbilityAttack init) | owner unchanged |
| 12 | first Separate_Update (visit 1028); exact-overlap draws 6f1d19f0 at 1028, 1029 | → 4273436052/209508436 → 3022241195/141606968 |
| 13 | attack orders: stream 9 ×3 (visit 1030); first Mover_UpdateSpeedAndHeading (visit 1031) | owner 3022241195/141606968 |

Unlocked (preference value 0): step 2 → 0, flags 0x2300400 at 29ec0d, step 5 Seed(owner, record seed) at 6f29ec33 and
693710(record seed); everything else identical in structure. After `SetRandomSeed(12345)` locked and unlocked runs give
identical values (int 407662142, int2 278, real 0.6505380), proving the only startup difference is the game seed.

## Behaviour per map (frozen in `expected-NUM-04.5.json`)
| Map (sha256) | Players 4..11 prefs | Race draws | Owner after races | Init int/int2/real word | Owner at 1st owner visit | 1st sep visit | 1st mover update |
|---|---|---|---|---|---|---|---|
| RS-SEP-02.2-triad (80bfb0e1…) | unset (random) | 8 | 4273436052/209508436 | (no queries) | 4273436052/209508436 | same | — |
| RS-NUM-04.5-mixed (18ff2d40…) | ORC,R,UD,R,NE,R,HU,R | 4 (p5→1,p7→3,p9→4,p11→4) | 4041395174/481053892 | 2130728302 / 929 / 0x3f03fb84 | 3946970562/277349488 | same | 3022241195/141606968 |
| RS-NUM-04.5-fixed (03ab71bf…) | all HUMAN | 0 | 2002874931/738765888 | 371411726 / 449 / 0x3f08a9aa | 3292812501/548955360 | same | 4261456605/413152424 |
| mixed, unlocked run 1 | as mixed | 4 (p5→1,p7→1,p9→2,p11→1) | 470151540/1690585192 (seed 82172056) | 735845865 / −290 / 0x3ba6ef00 | 4160792303/1486880788 | same | 3749371438/1351138512 |
| mixed, unlocked run 2 | as mixed | 4 | 2451431625/3027804256 (seed 82215974) | 436079602 / −575 / … | 2707349937/2824099852 | same | 2294369317/2688297160 |

Model checks (analyzer): triad 1484 owner + 47 stream draws, 3543 markers; mixed 16 owner + 53/61 stream draws; fixed 12 + 53/61;
unlocked 16 + 61/72; all `*-ok`, 0 mismatches. All draws on the single game thread.

## Engine actual startup vs retail (source reading, inference)
`G_InitLockedMapRandom` (games/warcraft-3/game/g_spawn.c:771) seeds and resolves races only when
`level.setup.map_flags & 0x8000`; map load `memset(&level,0,…)` (g_spawn.c:895) and setup init (g_spawn.c:915–931)
never set it, so production startup leaves `level.pathing_random = {0,0}` and keeps W3I races; only tests
(tests/t_movement.c:3470, 12830, 14983, 15398) force the flag. Retail default -loadfile startup is locked (0x8000 set).

## Engine entry points
1. `g_spawn.c` level setup (≈915–931): default `level.setup.map_flags |= 0x8000` for the test-map/-loadfile path (WorldEdit
   preference default 1); expose the unlocked choice and a setup seed (record seed: retail GetTickCount; engine: explicit
   setup input, e.g. a cvar/replay header) — owner decision on the engine-side source.
2. `G_InitLockedMapRandom` (g_spawn.c:771): generalize to a game-seed initializer: seed = locked ? 0x77617233 : setup seed;
   seed owner; reseed the 45 unit streams with the same word (NUM-04.6); resolve races for **both** branches.
3. `g_main.c:999` `G_StartScripts`: keep seed+races before `main` (retail: seed before load, races during load; no owner draw between).
4. Tests that force `map_flags|=0x8000` should instead exercise the default producer.

## Failing-regression expectations (inputs → outputs)
1. Default startup of a map with players 0–3 RACE_PREF_HUMAN and 4..11 = ORC,RANDOM,UNDEAD,RANDOM,NIGHTELF,RANDOM,HUMAN,RANDOM, **no forced flags**:
   `IsMapFlagSet(MAP_LOCK_RANDOM_SEED)` true; owner before main `4041395174/481053892`; races p5=Human, p7=Undead, p9=NightElf, p11=NightElf;
   main: `GetRandomInt(0,2147483647)=2130728302`, `GetRandomInt(-1000,1000)=929`, `GetRandomReal(0,1)` word `0x3f03fb84`; owner after `3946970562/277349488`.
2. Same with all 12 prefs fixed: owner at main `2002874931/738765888`; queries `371411726`, `449`, `0x3f08a9aa`; owner after `3292812501/548955360`.
3. Triad roster (players 4..11 unset): 8 draws → `4273436052/209508436` before the first separation visit, without forcing map_flags.
4. Unlocked with setup seed 82172056 (mixed prefs): owner after races `470151540/1690585192`, races p5=1,p7=1,p9=2,p11=1, queries 735845865/−290/0x3ba6ef00.
5. After `SetRandomSeed(12345)` from any of the above: `GetRandomInt(0,2147483647)=407662142`, `GetRandomInt(-1000,1000)=278`, real `0.6505380` (owner first draw 2689401862).

## Reproducer
```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research; cd /run/media/lofcz/ssd_external/GitHub/open-realm
python3 tools/frida/research/NUM-04.5_make_map.py --data /run/media/lofcz/ssd_external/Games/w3-research2 --variant mixed --name RS-NUM-04.5-mixed --tool build/bin/mpqtool --out $R/NUM-04.5/maps   # also --variant fixed
$R/_env/install-map.sh $R/NUM-04.5/maps/RS-NUM-04.5-{mixed,fixed}.w3m
$R/_env/live.sh $R/NUM-04.5/runs/batch1.sh '{DATA}' '{REMOTE}' '{DISPLAY}'                 # locked observe/control (quote the placeholders under zsh)
$R/_env/live.sh $R/NUM-04.5/runs/batch3-unlocked-key.sh '{DATA}' '{REMOTE}' '{DISPLAY}' '{ENV}'   # sets/deletes WorldEdit preference inside the lock
$R/_env/live.sh /home/lofcz/.local/share/uv/tools/frida-tools/bin/python tools/frida/research/NUM-04.5_trace.py observe --data {DATA} --map 'Maps\RS-SEP-02.2-triad.w3m' --preload-name sepres-triad.txt --marker-prefix PATHSEP --remote {REMOTE} --x11-display {DISPLAY} --seconds 45 --key-at 10 18 --output <new dir>
python3 tools/frida/research/NUM-04.5_analyze.py $R/NUM-04.5/captures/<dirs…> $R/SEP-02.2/captures/triad-control-1 --out $R/NUM-04.5/analysis-all.json
python3 tools/frida/research/NUM-04.5_expected.py --analysis $R/NUM-04.5/analysis-all.json --streams-oracle $R/NUM-04.6/oracle/streams-oracle.json --out $R/NUM-04.5/expected-NUM-04.5.json
```

## Provenance
game.dll `d51e5680…d8236`; Frida 17.18.0; environments B (27049/:98) and C (27050/:99, unlocked runs). Sources frozen in
`sources/` with `SHA256SUMS`: trace `7d0ce610…a77b`, observer `828cfe41…da5a` (triad-observe-1 used the first observer
revision `a1bdbab9…`, which lacked only the setup-marker/preference/seed-aggregation hooks), analyzer `8d1aa8d9…93d0`,
freezer `4e18e375…7b30`, map builder `14cffa64…f407` (imports sep_research_map `ac652093…dd2d`, base
PathingRE-MovementBypasses86b `4b9ea0ba…6555`). Maps: mixed `18ff2d40…20b2`, fixed `03ab71bf…72e8`, triad `80bfb0e1…b4a9`.
Seed source: the game's own setup decision in every run (no seed or state written by any tool). Analysis `analysis-all.json` `1e176bd5…c8b7`.

## Captures (capture.jsonl sha256)
| Capture | Kind | Result |
|---|---|---|
| triad-observe-1 | locked observe | `934b6620…5283`, complete; preload = SEP-02.2 triad-control-1/observe-1/-2 (env A) |
| mixed-observe-1, -3 | locked observe | `f6ade254…`, `31fb0af2…`; preloads identical to each other and to mixed-control-1 |
| fixed-observe-1, -2 | locked observe | `16a7bdd4…`, `529b17bb…`; identical to fixed-control-1 |
| mixed-control-1, fixed-control-1 | observer-free | complete; preload line-identical to observed runs |
| mixed-unlockedB-observe-1, -2 | unlocked observe (env C) | `4f725f84…`, `af872c18…`; different time seeds, modeled exactly |
| mixed-unlocked-observe-1, -2, mixed-unlocked-control-1 | **failed precondition** | preference written to `…\Warcraft III\WorldEdit` (wrong key): game read default 1 → locked; kept as extra locked repeats (identical preloads) |
| mixed-observe-2 | **incomplete** | stopped at the loading screen after init markers (176 rows, no preload); draws up to there modeled |
| mixed-unlockedB-control-1 | **incomplete** | no preload written (loading screen); unlocked control values are time-seeded anyway |
| batch1 attempt 1 | **failed launch** | zsh brace-expansion of unquoted placeholders, no process spawned (`runs/batch1-attempt1-FAILED-argexpansion.log`) |
Registry: both unlocked batches verified the key absent before, wrote REG_DWORD 0, and deleted the key afterwards (logs `runs/batch2.log`, `runs/batch3.log`).

## Public vs forced-state
All producers are public: map JASS (race prefs, natives), the WorldEdit test-map preference (a user setting). No memory
writes or synthetic inputs; Space keys only advance the loading screen.

## Exclusions
45-stream consumers/ownership/TLS: NUM-04.6. Network lobby transport (host stamp broadcast, w3g replay header path
`29dfb0` which seeds owner/streams from the record unconditionally): assembly only → proposed NUM-04.7. Observer slots
(`056a40` payload78 forcing Human without a draw): assembly only. Retail save/load of owner/streams: proposed NUM-04.8.

## Mismatches preserved
- Ghidra/MapPathfinding `6f29e300` "otherwise uses computed lobby seed" → corrected above (record seed; fold path unused).
- Engine doc "Unlocked/lobby seed, the default lock-flag producer and canonical player payload78 remain NUM-04.5": first two
  now recovered; payload78 remains assembly-only.
- GetRandomReal Preload prints 7 decimals truncated (0.5155565) while %.7f rounding of the same float gives 0.5155566; compare words.

## Proposed integration
`mapping-rows-NUM-04.5.txt`, `types-NUM-04.5.json` (WC3GameSetupRecord + 6 methods + 2 globals), `proposed-docs-NUM-04.5.md`;
Ghidra writes logged in `ghidra-writes.jsonl` (6 renames, 2 labels, 10 comments).
