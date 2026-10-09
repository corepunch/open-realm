<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/NUM-04.6/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **NUM-04.6**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/NUM-04.6/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-NUM-04.6.json` -> [`NUM-04.6-expected.json`](../../../../../tools/ghidra/fixtures/research/NUM-04.6-expected.json) (uncompressed sha256 `16afe54c8ce656b3f445a463535215372d88081057e4363d64710786d437a25c`, 187267 bytes)

> Research handoff for **NUM-04.6**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/NUM-04.6/`.
> Frozen expected results: `expected-NUM-04.6.json` (sha256 `16afe54c8ce656b3f445a463535215372d88081057e4363d64710786d437a25c`).
> Live captures are the NUM-04.5 set (`../NUM-04.5/captures/`, analysis `../NUM-04.5/analysis-all.json` `1e176bd5…c8b7`).

# NUM-04.6 handoff — the 45 CRandData unit streams (693710), their TLS producer and the separate generator classes

**Status.** Established: object identity, TLS ownership, creation/destruction, both reseed producers and the exact
stream arithmetic (A + O: Unicorn oracle 56 original calls word-exact against an independent port; L: every live stream
draw in 10 startups model-exact, including the 45-word tables after the locked seed, after two GetTickCount seeds and after
`SetRandomSeed(12345)`). Index ownership: 39 fixed indices classified by call site and RTTI (A); indices 2, 9, 32, 34, 35
exercised live through public JASS/orders (L). Separation of classes (L): pathing/separation/retry/race/public GetRandom*
draw only the path owner; CRandData draws are identical between maps with 8 and 0 owner race draws; audio and sprite
generators are client-local and their draw counts differ between identical runs. **Not established live:** the other
34 fixed indices and the 5 dynamic-index sites (A only); retail save/load of the streams; network transport of the seed.

**Correction to an existing claim.** Engine doc ("The full 693710 tail reseeds45 separate unit streams") and
`api_misc.h:1504` ("693710's separate per-unit streams"): the 45 streams are **not per-unit**. They are one per-game
`CRandData` object with 45 purpose-indexed streams (ability/buff/effect/attack/JASS/AI), shared by all units.

## Functions
| VA | Role | ABI (assembly) | Evidence |
|---|---|---|---|
| 6f693710 RandData_ReseedStreams | local=Seed(seed); stream[i]=Seed(Next(local)), i=0..44 | ECX seed, plain RET | A, O (8 seeds), L |
| 6f64f9a0 RandData_ReseedForGame | 693710 then empty 6744c0 | ECX seed | A, L (caller 6f29ec35) |
| 6f214140 Jass_SetRandomSeed | Seed(owner,s); d=Next(owner); jmp 693710(ECX=d) | cdecl stack4 | A, O (4 seeds), L |
| 6f693660 RandData_StreamRange | mulhi(Next(stream[i]), span) | ECX index, EDX span, EAX | A, O (18), L (32/34/35) |
| 6f6936a0 RandData_StreamUnitReal | out = (Next&0x7fffff\|0x3f800000) − 1 via software scalar (0 + f·(1−0)) | ECX out*, EDX index, EAX=out | A, O (16), L (2, 9) |
| 6f695c70 RandData_RollDice | out = scale·(dice + Σ + bonus); dice<1 → 0 + log | ECX out*, EDX index, stack dice/sides/bonus/scale*, RET 0x10 | A, O (10, dice 1..16), L (2) |
| 6f1ec180 PathRandom_RollDiceSum | dice≤16: Σ mulhi(Next,sides); >16: one Gaussian 1d12e0 sample | ECX sides, EDX dice, stack4 state, RET4 | A, O (≤16) |
| 6f65c7c0 RandData_Construct | alloc 0x16c, vtable 6fb7a69c (.?AVCRandData@@), 45× Seed(0), registry[ECX]=obj | ECX registry index | A, L (45 seeds at 6f65c839 during boot) |
| 6f693690 / 6f693620 RandData_Create / _Destroy | registry index 3; from 64f970 / 64f900 | none | A |
| 6f06c180 / 6f06c1e0 / 6f06c1b0 TlsSlots_Get / _Set / _AllocIndex | TlsGetValue([6fd3cb98])[slot]; TlsAlloc → 6fd3cb98 | ECX slot (EDX value) | A, L (one table pointer, one thread per run) |
| 6f045240 (default name) | creates slot 0xd registry (0x14 B, 20 entries, data at +0x10), then 046aa0 → 64f970 | — | A |
| 6f498960 AbilityAttack_GetDamageAmount | RollDice(stream 2, dice[+88+4w], sides[+94+4w], base+bonus, 1.0), clamp ≥0 | ECX ability, stack4 out*, RET4 | A (original method string), L |
| 6f4993b0 CAbilityAttack vfunc 194 | timer 0xd01c1 = base(6fd6bf40/44) × StreamUnitReal(2) at ability attach | — | A, L (one draw per created attacker) |
| 6f49ea80 Attack_HandleReactionDelayTask | timer 0xd01af = ReactionDelay × StreamUnitReal(9); stops unit | — | A, L |

## Layout / ownership
`TlsSlots_Get(0xd)` → engine registry {…, +0x10 data[20]}; `data[3]` = CRandData {+0 vtable, +4+8·i stream i
(WC3PathRandomState {sum,index}), i=0..44}, 0x16c bytes. Lifetime: game session (64f970 create during game start, before
29e300; 64f900 teardown). Thread: the game thread (one TID per run in all captures). There is no per-unit state.

## Seed producers (complete)
| Producer | Word | Evidence |
|---|---|---|
| RandData_Construct | 0 for every stream (state {0,0}) | A, L |
| 29e300 → 64f9a0 → 693710 | game seed: 0x77617233 when mapflag 0x8000 else setup-record seed (NUM-04.5) | A, L (locked + 2 unlocked) |
| 29dfb0 → 64f9a0 | record seed unconditionally (replay/other setup path, not exercised) | A |
| SetRandomSeed (214140) | first owner draw after Seed(owner,s) | A, O, L (12345 → 2689401862) |

## Stream index → consumer (static call sites; `static/stream-owners.json`)
| Idx | Owner (RTTI / function) | Helper | Level |
|---|---|---|---|
| 0, 44 | unit-type dice (687490 / 69e5b0; reads three per-type values, callers Transmute missile/buff 4efc30 and unit path 66a670) — bounty dice (inference) | dice | A |
| 2 | combat: CAbilityAttack attach jitter + GetDamageAmount dice; Bash/CriticalStrike/DrunkenBrawler/Evasion procs; Curse/CloudOfFog/DrunkenHaze/Silence miss; artillery 6d48b0 | unit/dice | L (jitter, dice) + A |
| 3 | Bash/CriticalStrike/DrunkenBrawler slot 73 | unit | A |
| 4 | 4eed60 (from 4ef0a0) | unit | A |
| 8 / 30 | CAbilityWander / CAbilityTornadoWander | unit | A |
| 9 | CAbilityAttack ReactionDelay (49ea80) | unit | L |
| 11 | 58ea20 / 58ec40 | unit | A |
| 12 | CAbilityMirrorImage | dice | A |
| 13 | AI captain 9d5e00/9b8690, unit 690490 → 9ce280 | dice | A |
| 14 | CBuffDarkConversion | unit | A |
| 16 / 17 | CAbilityRoar / CAbilitySentinel | unit / dice | A |
| 18 / 28 | CEffectStarfall + Monsoon/Starfall drain / CEffectMonsoon | unit | A |
| 19 | CAbilityDarkPortal | dice | A |
| 20, 24, 25, 26, 31, 37, 39 | 4d8c20; 630680; 6d6920 (25, 26); 501270; 524e00; 50e9b0/51e610 | mixed | A |
| 21 / 22 | CAbilityAlarm / CBuffDeathAndDecayAoe | unit | A |
| 23 | CEffectBallsOfFire / DeathAndDecay / FlameStrike point picks | unit/dice | A |
| 27 / 29 / 36 / 38 | CAbilityLocust / StoneSkin / SpellEffectBonus / PhoenixFire | unit/range | A |
| 32 / 34 / 35 | JASS ChooseRandomCreep / ChooseRandomNPBuilding / ChooseRandomItem + ItemEx (+54e000) | range | L |
| 33 | CAbilityMechanicalCritter | range | A |
| 40 / 41 / 42 / 43 | Demolish+WarStomp / ClusterRockets / CEffectVolcano / Defend+MagicDefense+PassiveDefense | unit | A |
| dynamic | 5d0840 (volcano: edx−0x3a, edx+0x20), 5d8930 (edx−8), 51e610 table, 9d5e00 (13+edi) | range/dice | A |
| unused statically | 1, 5, 6, 7, 10, 15 (may be reached by dynamic sites) | — | A |

## Other generator states (never the owner, never CRandData; `other_generators` in expected)
| State | Seed | Consumers | Live |
|---|---|---|---|
| owner [6fd53a48] (path) | 0x69707365 at boot, game seed at 29e300, SetRandomSeed | races, GetRandomInt/Real, overlap 1d19e0, retry, 28a460, 78e350 | model-exact, run-independent |
| AudioRandom 6fd6a674 | CRT 0, then **GetTickCount** at boot (3575be) | Audio_ChooseVariant, 342320 (PlayLabel path), Audio_PlayWarcryResponse, 358c70 | 2–98 draws (triad 2, probes 80–98), differs per run |
| SpriteAnimationRandom 6fd685ac | CRT 0x53705269, never reseeded | Sprite_ResolvePendingAnimation → 1c0c40, 9aa3c0 | 222–431 draws, differs per run |
| 6fd684dc / 6fd684fc, emitter +0x60 states | CRT 0 / per-emitter (1ba7b0, 1ba990) | CParticleEmitter2 / CPlaneParticleEmitter / CWeatherEmitter | seeds only in window |
| LightningJitterRandom 6fd77d18; 6fd6b32c | CRT 0; 6fd6b32c Seed(object pointer) at 3a04bb | 9a3b40 lightning points; UI frame | seeds only |

## Behaviour (frozen)
- `war3` table (locked seed): stream 0 `742823453/1018430624`, 2 `3292812501/1419794628`, 9 `2863058991/2362214472`, 32 `974890823/2959360188`,
  34 `3738524401/1144280128`, 35 `3410436445/1751154732`, 44 `2207432243/1489813704` (all 45 in expected `oracle.reseed[0]`).
- After `SetRandomSeed(12345)`: owner `2689401862/2025332800`, reseed word 2689401862, stream 2 `174291618/940848284`, 32 `3253384010/1221381144`,
  35 `2067508197/344244224` — the live first draws after the reseed (mixed-observe-1 seq 738/739/744) start exactly at these words.
- Unlocked runs: table = reseed45(record seed), e.g. seed 82172056 → stream 2 `2089476521/809518192`, 32 `708566365/345268236`.
- Mixed vs fixed maps (4 vs 0 owner race draws, different owner words): all stream-2/9/32/34/35 draws identical in words and owner-visit timing.

## Engine entry points
1. New owned state `level.unit_random[45]` (`wc3Random_t`), seeded by a port of 693710 from the NUM-04.5 game seed at
   startup (`g_spawn.c` G_InitLockedMapRandom generalization) and saved/restored with Save (next format; reject older).
2. `api/api_misc.h:1498` `SetRandomSeed`: keep owner seed + one draw, then reseed all 45 streams from that draw; **delete `srand(seed)`**.
3. Replace `rand()` consumers with the owning stream (port arithmetic exactly: range = mulhi, unit real = low-23 mantissa − 1, dice = Σ mulhi + dice + bonus, Gaussian above 16 dice):
   - `skills/s_attack.c:188` damage dice → stream 2 RollDice; `s_attack.c:405` curse miss, `:416` bash → stream 2 unit real;
     `skills/s_hero_passives.c:1012/1014/1022/1048` crit/evasion/drunken brawler → stream 2 (slot 73 procs stream 3);
     `skills/s_human_abilities.c:712` Defend deflect → stream 43; `skills/s_creep_abilities.c:283` hardened skin → verify (43 candidate);
     `skills/s_dark_portal.c:10` → stream 19 dice; `skills/s_area_spell.c:86–87` → 23 (Balls of Fire/D&D/Flame Strike), 42 Volcano, 41 Cluster Rockets;
     `api/api_misc.h:1469` ChooseRandom* → 32 creep, 34 NP building, 35 item/itemEx (range = candidate count);
     `skills/s_summon.c:175` Rain of Chaos, `skills/s_requested_abilities.c:279` bounce choice, `g_destructable.c:303/313` drop rolls → owner not yet identified (keep explicit TODO, do not use the path owner).
   - attack attach jitter (stream 2, one draw per unit with CAbilityAttack at creation) and ReactionDelay (stream 9) if/where the engine models them.
4. Presentation/audio (`g_sound.c:335/623`, `g_commands.c:606/658`, `skills/s_harvest_lumber.c:639/641`, `g_model.c:828/844`, `g_monster.c:236`)
   must use a client-local generator: never the owner, never the 45 streams, not saved, not seeded from the game seed.

## Failing-regression expectations
1. Default startup (any map): `unit_random[i]` equals `oracle.reseed[0].streams[i]` for all 45 i (e.g. i=2 `3292812501/1419794628`).
2. Startup with setup seed 82172056 (unlocked): stream 2 `2089476521/809518192`, 32 `708566365/345268236`, 35 `810706540/1017954340`, 34 `3524263839/1859592`.
3. `SetRandomSeed(12345)`: streams equal `oracle.set_random_seed[0].streams` (stream 2 `174291618/940848284`); owner after `2689401862/2025332800`; no libc `srand`.
4. In the mixed probe: tick-1 `ChooseRandomCreep(1)`, `ChooseRandomItem(1)`, `ChooseRandomItemEx(ITEM_TYPE_PERMANENT,2)`, `ChooseRandomNPBuilding()` advance only streams 32, 35, 35, 34
   (words: 32 `974890823/2959360188→999415454/2891458720`; 35 `3410436445/1751154732→3062064831/1683253264→2356487499/1615352040`; 34 `3738524401/1144280128→2101011714/1076378660`)
   and leave the owner `3946970562/277349488` unchanged; results creep `1851941230`, item `1953066612`, itemEx `1918989366`, npb `1852665711` (retail ids; depend on item tables).
5. Creating six attacking units advances stream 2 by six draws (first `3292812501/1419794628→3490513860/1351893160`) and the owner by none.
6. Stream words are unchanged by owner consumption: the same scene with 0 vs 4 race draws produces identical stream draws.

## Reproducer
```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research; cd /run/media/lofcz/ssd_external/GitHub/open-realm
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_NUM-04.6_streams.py --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report $R/NUM-04.6/oracle/streams-report.json --fixture $R/NUM-04.6/oracle/streams-oracle.json
(cd $R/NUM-04.6/static && python3 classify_callsites.py)      # needs Ghidra read API; regenerates stream-callsites.json
python3 tools/frida/research/NUM-04.6_expected.py --oracle $R/NUM-04.6/oracle/streams-oracle.json --analysis $R/NUM-04.5/analysis-all.json --owners $R/NUM-04.6/static/stream-owners.json --captures $R/NUM-04.5/captures/{triad-observe-1,mixed-observe-1,mixed-observe-3,fixed-observe-1,fixed-observe-2,mixed-unlockedB-observe-1,mixed-unlockedB-observe-2} --out $R/NUM-04.6/expected-NUM-04.6.json
# live captures: see NUM-04.5 HANDOFF (same observer records every stream seed/draw)
```

## Provenance
game.dll `d51e5680…d8236`. Oracle script `71b671f8…dd2d` (copy in `static/`), oracle fixture `5573027c…a085`, report `ee4f3838…51c9`;
freezer `0da4c675…b0`; call-site table `acd80544…f40e`, owners `2539a66c…f8d4`. Captures/maps/observer: NUM-04.5 provenance.
Stand-in (oracle only): `6f06c180` TLS lookup returns a synthetic registry; all generator/scalar arithmetic is original code.

## Captures, controls, failures
As NUM-04.5 (10 observed startups: 7 locked complete/partial, 2 unlocked, plus 2 locked repeats from the wrong-key batch);
observer-free locked controls line-identical (the ChooseRandom* ids printed by JASS depend only on streams 32/34/35).
Failed/incomplete runs are listed in the NUM-04.5 handoff.

## Public vs forced-state
Live: public JASS only (ChooseRandom*, CreateUnit, IssueTargetOrder/IssuePointOrder, SetRandomSeed). Oracle: original
code with the single TLS stand-in; no gameplay state forced.

## Exclusions
Live coverage of the remaining 34 fixed + dynamic indices (proposed NUM-04.9, ability-by-ability); retail save/load of
CRandData (vfunc 0 is an empty loop and vfunc 1 a debug dump — neither serializes; proposed NUM-04.8); network
seed transport (NUM-04.7, see NUM-04.5); exact engine ports of Defend/Hardened Skin/Rain of Chaos/bounce/drop rolls (owner
unidentified for the last three).

## Mismatches preserved
- "per-unit streams" wording (engine doc, api_misc.h TODO) → per-game, purpose-indexed streams.
- `6f49ea80` already named `Attack_HandleReactionDelayTask` (ORDER-01.10); my rename was refused by gw.py, the existing name is kept and the comment appended.
- Stream draw counts after the probe's `complete` marker differ between repeats (38/46/58 stream-2 draws): the capture keeps running
  combat after the marker for a different wall time; every recorded draw is model-exact and the shared prefixes are identical.

## Proposed integration
`mapping-rows-NUM-04.6.txt`, `types-NUM-04.6.json` (WC3RandData 0x16c with 45 WC3PathRandomState fields; 9 new
WC3AttackRangePrefix fields; 13 methods; 4 globals), `proposed-docs-NUM-04.6.md`; Ghidra writes in `ghidra-writes.jsonl`.

## Payoff172 implementation

All45 purpose states are game-owned and saved in format139. Map initialization
seeds them from a local generator using the same fixed/stored setup word as the
path owner; race draws cannot alter their initial states. Public SetRandomSeed
executes its one owner draw and reseeds all45 states from that result, with no
libc/presentation seed side effect. ChooseRandomItem/ItemEx use purpose35 and
unsigned multiply-high selection. Boot/reload, all45 saved positions and actual
public item-query continuation are production regressions. This closes the
NUM-04.6 state-ownership/seed-side-effect contract; it does not claim that every
ability/native consumer listed above is migrated or retail exact.

Fresh inspection of4993b0 corrects the prior attach-jitter description: the timer
is **base multiplied by unit-real**, not base plus unit-real. The draw count
evidence is unaffected. Attach/reaction timer integration is not claimed here.
