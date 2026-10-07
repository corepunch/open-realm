<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-02.2/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **MAP-02.2**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-02.2/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-MAP-02.2.json` -> [`MAP-02.2-expected.json`](../../../../../tools/ghidra/fixtures/research/MAP-02.2-expected.json) (uncompressed sha256 `41b43cecd57808968f2193cddb43c16f4713141745a92c167799918bd856b854`, 98057 bytes)

# MAP-02.2 handoff: file-backed cliff, water-boundary and bridge fixtures

**Status.** This is a research handoff. It does not close the TODO. The following are established:

- **Live retail capture (5 complete repeats):** initial fine cells and hierarchy classes for every lane in one file-backed copied map with:
  - a cliff step (cliff level 2 to 3);
  - a dry/shore/shallow/deep water boundary;
  - a stock walkable bridge destructible (LT06; LT04 in exploratory layout v1).

  The same captures record the support-height source chosen for foot, hover, float, amph and fly units at 8 points. A JASS-only observer-free control matches all 193 markers.
- **Original-code oracle:**
  - Unicorn, 8068 cases, 0 mismatches.
  - Covers the support getter `66d780`, terrain/deck `78d1e0`, GetLocationZ core `64f7e0` and the deep-water predicate `64eca0`.
- **Not established:**
  - The placement support level `78bc90`/`654060` on bridges.
  - The kernel of the air-height field (`7434b0`).
  - Whether a WorldEdit-saved WPM bakes deck walkability.
  - Ground units' physical trajectories (E2E).

## Functions

| VA | Role | ABI (from assembly) | Evidence |
|---|---|---|---|
| 6f04c860 PathMaps_Load | File-backed WPM to fine top byte | existing | live: map-load hook (copied from wc3_pathfinding.js) |
| 6f684480 Unit_RefreshSupportPosition | Support refresh | thiscall ECX unit, [esp+4] out xyz*, [esp+8] force; RET 8. Calls vt+e4 (layer -1, out=&force slot) at 6f68458e, sets Unit+280 bit2 at 6f684599/5a2, then calls 64eca0 at 6f6845b9 and sets bit20 at 6f6845c2/5cb, then stores 284/288/28c. | asm; live 5 runs |
| 6f66d780 CUnit_GetSupportZ (vt 6fb77eb0+e4) | Unit support Z | thiscall ECX unit, [esp+4] point*, [esp+8] layer (to ECX of 78d1e0), [esp+c] out deck flag*, [esp+10] force; RET 0x10; result x87 ST0 | asm; oracle 7680 cases; live |
| 6f78d1e0 Terrain_GetSupportHeightWithDeck | Terrain or walkable-deck Z | fastcall ECX layer, EDX out flag* (nullable), stack x, y, flag; RET 0xc; ST0. 7437c0(point, layer), then 782a80 if layer∈{-1,-2} or flag≠0 and the deck is strictly higher (`COMISS`/`JBE`). | asm; oracle 120; live |
| 6f64f7e0 Terrain_GetLocationSupportZ | GetLocationZ core | ECX/EDX forwarded to 78d1e0; stack x, y, flag; RET 0xc; ST0. max(ground/deck, water strictly higher). | asm; oracle 12; live JASS |
| 6f200cd0 Jass_GetLocationZ | Native | 64f7e0 with ECX=-1, EDX=0, flag 1; float bits in EAX | asm (registration 6f207e72); live |
| 6f78bd60 Terrain_GetWaterHeightAtPoint | Water surface | fastcall ECX point*, EDX out float*; EAX present; RET | asm; live |
| 6f74b690 | Terrain water cell test | thiscall terrain, point*, out*; out=((raw>>…)\|0x45800000)-6144+map offset | decompile + live value -0.1 for raw 8550 |
| 6f64eca0 PathWorld_IsDeepWaterPoint | Deep flag | stdcall (x, y) RET 8: 04e090(mask 02,0) && !04e090(mask 40) | asm; oracle 256 |
| 6f7437c0 Terrain_GetHeightForLayer | Height per layer | thiscall terrain, point*, layer; RET 8; -3 goes to 7434b0 (air field), -1/-2 to 73c800 | asm + live |
| 6f782a80 WalkableDeck_GetHeightAtPoint | Deck raycast | point*, out*; RET 8; EAX hit; out=2560-t | decompile + live |
| 6f68f390 (vt+e8) | Fly-height getter | owned by BASE-02.1 | live value only |
| 6f68c190 | Structure predicate used by 66d780 | thiscall, push 1, RET 4: 5c&10000 && (!bit7 or 5c&8000000) | asm; executed in oracle |

Ghidra writes are in `ghidra-writes.jsonl`, using the script `ghidra-writes-MAP-02.2.sh`:
- **Renames:** 6 functions.
- **Comments:** 9 (3 appended to existing comments).
- **Not saved.** The coordinator saves.

## Fields

| Field | Meaning | Write | Read | Evidence |
|---|---|---|---|---|
| fine cell word >>24 | WPM-mapped static lanes. Bits: 02 walk, 04 fly, 08 build, 10 aux, 20 blight, 40 float, 80 amph, 01 any | 04caba loop during 04c860 | queries | live |
| hierarchy word bits (31-2i..30-2i) | Lane class (0 clear, 1 blocked, 2 mixed), with i=0 ground, 1 amph, 2 float, 3 fly | 15cf80/15d470 | adaptive | live (decoding validated: dry→float=1, deep→ground=1, cliff→ground/amph/float=1) |
| CUnit+1fc | Movement bits: 1 foot, 2 fly, 8 hover, 10 float, 20 amph | rebind/init (BASE-02.1) | 66d780 6f66d949 (&0x38), 6f66d960 (==0x20) | live 1/8/10/20/2 |
| CUnit+200 | Forced-ground counter | 6877b0/69c840 | 66d780 6f66d8d2 (`JG` skips flyer blend) | asm; oracle |
| CUnit+210 | Fly-height max (moveHeight) | rebind | 66d780 6f66d901 | live 360.0 (0x43b40000) |
| CUnit+280 bit2 | On walkable deck | 684480 6f684599/6f6845a2 | 66d780 6f66d790 | live |
| CUnit+280 bit8 | Multi-sample/building support path | (not traced) | 66d780 6f66d7aa/6f66d7db | live (hgry 0x18), oracle |
| CUnit+280 bit20 | Deep water (walk blocked, float clear) | 684480 6f6845c2/6f6845cb, **after** the getter | 66d780 6f66d969 (amph only) | live lag, oracle |
| CUnit+280 bit10 | Set on all ground probe units | not traced | — | live only, meaning unknown |
| CUnit+284/288/28c | Cached support x, y, z | 684480 6f684622/6f68462b | 66d780 6f66d7f2/825/862 | live, oracle |
| CUnit+5c bit 20000000 | Flying support blend | rebind (BASE-02.1) | 66d780 6f66d8c6 | live (hgry) |

## Fixture (layout v4/v5; 17x17 vertices, 64x64 fine cells, origin 0,0)

- **Water band** (vertex rows 0..10):
  - shallow vertices: columns 4, 5, 11, 12 (h=0x2000-256 → Z -64);
  - deep vertices: columns 6..10 (Z -192);
  - water raw 8550, flag 0x40.
- **Cliff:** vertex rows 12..16, columns 8+ at cliff level 3.
- **Bridge:** LT06 at (1024, 640).
- **Authored WPM:**
  - dry 40;
  - shallow 08 (tiles with any water corner);
  - deep 0a;
  - cliff tiles ca.
- **Variants:**
  - `blank`: WPM all 00;
  - `deckwalk`: authored, plus the 352 LT06 deck cells set to 08.

## Behaviour / frozen results

The full expected results are in `expected-MAP-02.2.json`, sha256 `41b43cecd57808968f2193cddb43c16f4713141745a92c167799918bd856b854`.

**Loader.**
- The top byte is exactly the WPM mapping: 40→41 (2128 cells), 08→09 (1120), 0a→1b (640), ca→db (208). The blank variant gives all 4096 cells = 00.
- **Nothing is derived from W3E water or cliffs** (hierarchy is also all 0 in blank).
- Object creation leaves the top byte unchanged. There are 0 links at load.

**Cells and hierarchy (authored).** Hierarchy classes are given as ground/amph/float/fly per level L0..L3.

| Point | Fine cell | Top byte | Blocked lanes | Bridge objects | Hierarchy L0..L3 |
|---|---|---|---|---|---|
| dry (256,192) | 8,6 | 41 | float | – | 0010,0010,0020,0020 |
| cliff bottom/top | 20,58 / 44,58 | 41 | float | – | 0010 ×3, then 2210 / 0010 |
| shore / shallow | 14,6 / 18,6 | 09 | build | – | 0000 (shore L2-3 0020) |
| deep (1024,192) | 32,6 | 1b | walk, build | – | 1000,1000,1000,2000 |
| bridge_shallow (576,640) | 18,20 | 09 | build | 08 | 0000 ×3, 2000 |
| bridge_deck (1024,640) | 32,20 | 1b | walk, build | 08 | 1000 ×3, 2000 |

**Bridge objects.** Category 08 covers all texture cells. Categories 10 and c2 cover the 224 rail (magenta) cells.

| Bridge | Fine-cell extent | Note |
|---|---|---|
| LT06 (fixedRot 90) | x16..47, y12..29 | Texture orientation |
| LT04 (fixedRot 0) | x24..41, y4..35 | Rotated 90°. The doo angle 1.5708 is ignored (v3). |

**Walkability.**
- Static walk is never cleared by the deck.
- Public Footman, Move from (192,640) to (1856,640):
  - authored WPM: detours to y≈1328 through shallow water (max |Δy| 688);
  - blank and deckwalk WPM: crosses the deck (|Δy| ≤ 43.7, GetLocationZ up to 81.729).

**Support Z at each point** (authored, at-point refresh, layer -1). Source codes:
- T = terrain;
- W = water;
- D = walkable deck (Unit+280 bit2 = 1);
- F = flyer blend;
- b20 = deep-water flag set.

| Type (movetp bits) | cliff_bottom | cliff_top | dry | shore | shallow | deep | bridge_shallow | bridge_deck |
|---|---|---|---|---|---|---|---|---|
| hfoo foot (1) | 0 T | 128 T | 0 T | -32 T | -64 T | -192 T b20 | 10.05 D | 84.047 D b20 |
| hsor hover (8) | 0 T | 128 T | 0 T | -0.1 W | -0.1 W | -0.1 W b20 | 10.05 D | 84.047 D b20 |
| hbot float (10) | 0 T | 128 T | 0 T | -0.1 W | -0.1 W | -0.1 W b20 | 10.05 D | 84.047 D b20 |
| nmyr amph (20) | 0 T | 128 T | 0 T | -32 T | -64 T | **first -192 T, then -0.1 W** b20 | 10.05 D | 84.047 D b20 |
| hgry fly (2) | 360 F | 488 F | 391.5 F | 428.243 F | 438.729 F | 415.944 F | 540 F | 488 F |

- **JASS GetLocationZ:** 0, 128, 0, -0.1, -0.1, -0.1, 10.05, 84.047.
- **GetUnitFlyHeight:** 0, except hgry = 360. That value comes from the base map's w3u `umvh` 360, not the SLK value 240.
- **Blank variant:**
  - bit20 is never set, so amph stays on the seabed (-192) in deep water;
  - deep foot is -192;
  - all other values are identical.
- **Flyers:**
  - The final out flag comes from the layer -3 call, so Unit+280 bit2 = 0 even above the deck.
  - The air field is 31.5 at dry, 55.9 at deep, 128 at the deck and 180 at the deck end. It is raised near the LT06 bridge (flyH 256). In v1 it was 0 at dry and 96 at deep.
- **Footman walking onto the deck (deckwalk):** support descends on terrain to -61.6 in shallow water, then switches to D at 3.566 (bit2 set) and rises to 84.

## Reproducer (repository root)

```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-02.2; T=$R/tools/bin/mpqtool   # owner build copy + libs, SHA256SUMS
B=/GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/Human02Interlude-original.w3m
python3 tools/frida/research/map022_make_maps.py --base $B --tool $T --variant authored --bridge-code LT06 --bridge-angle 1.5707964 --output $R/maps/RS-MAP-02.2-authored-v4.w3m
python3 tools/frida/research/map022_make_maps.py --base $B --tool $T --variant blank    --bridge-code LT06 --bridge-angle 1.5707964 --output $R/maps/RS-MAP-02.2-blank-v4.w3m
python3 tools/frida/research/map022_make_maps.py --base $B --tool $T --variant deckwalk --deck-cells $R/static/lt06-deck-cells-v4.json --bridge-code LT06 --bridge-angle 1.5707964 --output $R/maps/RS-MAP-02.2-deckwalk-v5.w3m
cp -n $R/maps/RS-MAP-02.2-*-v4.w3m $R/maps/RS-MAP-02.2-deckwalk-v5.w3m /run/media/lofcz/ssd_external/Games/w3-research/Maps/
# one live run (modes: observe | control); repeat with new --output names
flock /GitHub/wc3-analysis/reports/pathfinding-1.27/research/_env/live.lock /home/lofcz/.local/share/uv/tools/frida-tools/bin/python \
  tools/frida/research/map022_capture.py --data /run/media/lofcz/ssd_external/Games/w3-research --map 'Maps\RS-MAP-02.2-authored-v4.w3m' \
  --variant authored --mode observe --remote 127.0.0.1:27048 --x11-display :97 --seconds 420 --continue-at 80 --output $R/captures/observe-authored-v4-1.jsonl
python3 tools/frida/research/map022_summarize.py $R/captures/observe-authored-v4-1.jsonl $R/captures/observe-authored-v4-1-summary.json
python3 tools/frida/research/map022_expected.py --report-dir $R
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_map022_support.py --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report $R/oracle-support-MAP-02.2.json
```

Rebuilding with the current builder (4090f3b9…) reproduces the v4 and v5 map sha256 values byte-for-byte (checked in /tmp).

## Provenance

- **Binaries:** game.dll d51e5680…d8236. The CRT and Storm hashes are in `_env/binary-hashes.txt`. Frida 17.18.0.
- **Base map:** Human02Interlude-original.w3m, sha256 in each `maps/*.json`.
- **mpqtool:** a14bab49… (libs listed in tools/SHA256SUMS).
- **Maps:**

  | Map | sha256 |
  |---|---|
  | authored-v4 | 86b9235c…c90b |
  | blank-v4 | 2471f51e…c0e8 |
  | deckwalk-v5 | 660bc6a9…0e70 |
  | v1 authored / blank | 81d18ce4… / 32a2c0e1… |
  | v2 authored / blank | 3631b685… / f68fbdb9… |
  | v3 authored / blank | 525fe2ba… / 09275c4a… |

- **Scripts (sha256):**

  | Script | sha256 |
  |---|---|
  | map022_make_maps.py | 4090f3b9… |
  | map022_probe.j | 6cd3fd24… |
  | map022_observer.js | d3b33585… |
  | map022_capture.py | b33188a0… |
  | map022_summarize.py | 265e13f2… |
  | map022_expected.py | 153776f4… |
  | verify_map022_support.py | ff32f3bf… |

- **Oracle report:** `oracle-support-MAP-02.2.json`, dfcb46a9… (8068 cases, 0 mismatches).
- **Deck cell list:** `static/lt06-deck-cells-v4.json`, da0f4fe0…. It is derived from observe-authored-v4-1 as the LT06 08 cells minus the c2 cells.
- **Seed:** none. The probe is deterministic and timer-driven, and the repeat is marker-identical.

## Captures (all retained in `captures/`)

| Capture | Map | Status |
|---|---|---|
| observe-authored-1 | v1 | **failed**: observer JS syntax error. Not evidence. |
| observe-authored-2 | v1 | **incomplete**: a single Space key was sent before the loading prompt, and only the start marker was recorded. Not evidence. The loader and start-snapshot sections are valid but unused. |
| observe-authored-3 | v1 (LT04 angle 0) | Complete. 162 markers. Deck runs along y over the channel; the 'deep' point lies on the deck end (9.556). Exploratory, superseded by v4. |
| observe-authored-v3-1 | v3 (LT04 angle π/2) | Complete. Shows the doo angle is ignored (footprint identical to v1). |
| observe-authored-v4-1 | v4 | **Primary.** Complete, 193 markers. |
| observe-authored-v4-2 | v4 | Repeat. Markers, support table and cell digest are equal to v4-1. |
| control-authored-v4-1 | v4 | Observer-free control (spawn/resume/key/kill only). All 193 markers equal to v4-1. |
| observe-blank-v4-1 | blank v4 | Complete. |
| observe-deckwalk-v5-1 | deckwalk v5 | Complete. |

Preload-file sha256 values differ between runs, but the extracted MAP022 markers are equal. The `-previous-preload.txt` files preserve the earlier output.

## Observer controls

- One JASS-only control run (`control-authored-v4-1`) and one observed repeat. Markers (positions, GetLocationZ, fly height, orders, crossing path) are identical across observed run 1, the repeat and the control.
- All hooks only read memory. The mid-function probes are 684597, 66d7b3, 66d862, 66d8be, 66d914, 66d949, 66d982, 78d21e and 78d25a.
- Under hooks the loading time grows past 80 s. The controller therefore repeats Space every 15 s, at most 6 times, until the first tick. The control received 6 keys.

## Exclusions

| Item | Owning task ID |
|---|---|
| Placement support level on bridges (`78bc90` → `782bf0`, `654060`), rejected-level controls | FOOT-04.2/04.3 |
| Movement-type producers, fly-height getter/morph and Unit+1fc/5c/200 writers | BASE-02.1 (parent handoff) |
| Bridge object categories 08/10/c2 eligibility per consumer | BASE-02.2 / FOOT-03.1 |
| Path-texture rotation convention (LT04 fixedRot 0 yields a rotated footprint) | relates to MAP-02.3 (closed). Recorded here only. |
| Full crossing trajectories and repulsion | E2E |
| Air-height field producer | proposed MAP-02.5 |
| WE-baked deck WPM | proposed MAP-02.4 |

## Mismatches preserved

1. `naval-movement.md` says "A live walkable bridge may clear walking on its authored deck". In these file-backed fixtures the deck **never** clears static walk. Deck walkability comes only from WPM: authored 0a under the deck causes a detour, while 00/08 allow a crossing. Either WE bakes the deck into WPM (MAP-02.4) or the earlier claim refers to another mechanism.
2. The LT04 doo angle π/2 is ignored, and its footprint is rotated 90° relative to LT06. fixedRot governs orientation.
3. Amphibious first refresh in deep water uses the stale bit20: Z is -192, and only a later forced refresh gives -0.1. This applies to every amph deep run (authored, deckwalk, repeat).
4. hgry fly height is 360, not the SLK value 240, because the base map's w3u overrides `umvh`. This is not an engine issue.
5. Exploratory layouts v1–v3 put the 'deep' point on the deck end (Z 9.556). Those rows are kept but superseded.

## Suggested failing engine regressions

1. **Load lanes from WPM only.** A file-backed map with W3E water/cliffs and WPM all 00 should give a fine top byte of 00 everywhere and hierarchy 0. With authored WPM the mapping should be 40→41, 08→09, 0a→1b, ca→db.
2. **Walkable bridge.**
   - LT06 at (1024,640) creates objects 08 (576 cells, x16..47, y12..29) and 10/c2 (224 rail cells).
   - The deck cell (32,20) keeps top byte 1b.
   - Footman Move (192,640)→(1856,640) must detour (|Δy| > 600) with authored WPM, and must cross (|Δy| < 50) when the deck cells are 08.
3. **Support Z for the 5×8 table above.** Exact floats: -0.1 = water raw 8550; 84.047 deck. Include:
   - the amph first-refresh -192 → -0.1 lag;
   - blank-WPM amph deep = -192;
   - float/hover water max;
   - flyer bit2 = 0 above the deck.
4. **GetLocationZ = max(ground-or-deck, water)** at the 8 points.
5. **Oracle-driven unit tests of the `66d780` rule.** The 7680 cases in `oracle-support-MAP-02.2.json` cover:
   - structure bit8;
   - cached Z with force 0;
   - invalid movetp bits 0x18, which still float on water.

   The `max ≤ 0.01` (unscaled) flyer branch is not exercised: every case uses max 360.
