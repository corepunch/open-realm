# Retail cursor rendering

## Scope and evidence

This maps **Warcraft III 1.27.1.7085**, `game.dll` SHA256
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`.
Addresses are preferred VAs at image base `0x6f000000`; Frida uses RVAs.
These are recovered descriptions, not original function names. Class names come
from the binary's vtable/RTTI information.

The investigation followed direct calls, data xrefs to virtual methods, the
actual vtable slots, an initially undefined traversal callback, and live Frida
calls in a separate Wine/Xvfb Human01 session. It extends the input/state work in
[scroll-cursors.md](scroll-cursors.md). The map covers cursor ownership through
model animation and Direct3D indexed submission; it does **not** claim complete
recovery of every shared model-renderer feature or every cursor-mode producer.

Durable tools:

- `tools/ghidra/MapCursor.java`: hash/base checked function names, comments,
  virtual-slot annotations, and partial frame/sprite data types. Applied and
  saved in the analysis Ghidra project. Unknown fields remain undefined and
  inferred decompiler prototypes are deliberately not imposed on the program.
- `tools/frida/trace_wc3_cursor.py` and `wc3_cursor.js`: bounded attach-only
  observer for cursor selection, frame/animation state, shared draw path and
  actual D3D state. It does not call gameplay functions or change target data.

Raw decompilation, disassembly, xrefs and traces stay outside the repository in
`/GitHub/wc3-analysis/reports/cursor/`. Do not make tests depend on that directory.

## Ownership and resource selection

`CScreenFrame` owns a lazily allocated `CCursorFrame` at **screen +0x16c**.
`0x6f11dd70` allocates **0x1b0 bytes**, then `0x6f11d380` constructs the
`CSpriteFrame` subclass with draw layer **10000**. Main vtable:
`0x6fa8da14`; the layout subobject at +0xb4 uses `0x6fa8db04`.

A layer's cursor sprite is separate from the cursor frame: `0x6f0f0a70` replaces
the reference at **layer +0x2c**, retains/releases objects, and calls virtual
slot +0x84 to propagate the change. `0x6f0ef550` handles active-layer and child
propagation; `0x6f0f0ab0` obtains the screen cursor frame and binds the sprite.
`0x6f11d580` calls the generic first-sprite setter `0x6f0f7770` with model-camera
index **-1**, bypassing authored model-camera extraction.

`0x6f2e0340` creates an **Uber** sprite with `0x6f1a2de0(1)`, resolves skin key
`Cursor` through `0x6f342220`, loads it through `0x6f1a47b0`, selects Normal,
binds it, and releases its temporary reference. The skin lookup checks
CustomSkin, the active skin, and the default skin. `0x6f303410` includes cursor
recreation in race/UI refresh. GameUI construction also creates its own cursor
sprite (`0x6f367710`); glue and gameplay do not simply share one immortal object.
Observed resource: `UI\\Cursor\\HumanCursor.mdl`, resolved to the archived MDX.

The cursor therefore needs a model instance, a sequence queue/clock, tint and
reference lifetime. Loading a texture named Cursor is insufficient. The current
engine's hardcoded HumanCursor is still a known limitation for race/CustomSkin
parity.

## Position, projection and visibility

The cursor update virtual slot **+0x2c** points to `0x6f11d520`. Its data xref is
`0x6fa8da40`; a search for direct callers alone misses this entry point. It checks
the cursor-visible global through `0x6f0ed6e0` and the first sprite before calling
`0x6f11d5a0` and the base sprite-frame update.

Position flow:

```text
11d5a0 cursor position
  -> 072e80 thread event context and lock
     -> 075540 client pixel position
        -> 091150 OS/client-coordinate query
        -> 0753a0 normalize against client rectangle
        -> 8b38a0 scale to UI coordinates
  -> 1ac940 vector-track setter, track 1, retain-axis mask 0
```

For client dimensions `w,h` and client pixels `x,y`, the resulting translation is
`(0.8*x/w, 0.6*(1-y/h), 499.9989929199219)`. Z is literal bits `0x43f9ffdf`.
`0x6f0753a0` also supports a clip rectangle: points on/outside its boundary are
clamped one pixel inside and the pointer is warped. This is distinct from the
gameplay edge-scroll test.

No extra screen-edge centering or directional hotspot offset is added here.
The MDX geometry and animation supply the visual offset from the translation.
Live data confirmed translation track **1** at Uber **+0xc0**, scale **1**, and
null model camera. Be careful: `1ac940` pushes the number **3** to the generic
track setter as vector width; it is **not** selecting track 3.

Default frame projection uses `0x6f1add30`: orthographic near/far -500/+500,
Z inversion, and frame-center transforms. The cursor's Z sits just inside that
volume. Treating this as world-camera geometry would misplace or occlude it.
The model remains clipped by the viewport at screen boundaries.

## Animation and mode state

`0x6f3528e0` registers token IDs:

| Token | ID |
|---|---:|
| Normal | 0 |
| Select | 1 |
| Target | 2 |
| TargetSelect | 3 |
| InvalidTarget | 4 |
| HoldItem | 5 |
| Scroll | 6 |
| Left / Right / Up / Down | 7 / 8 / 9 / 10 |

These IDs are **not MDX sequence indices**. HumanCursor has Target at sequence
index 0 and Normal at index 1. Matching is through the tokenizer/animation set.

`0x6f38a9c0` selects modes as follows, then stores owner +0x1ac and applies tint
through `0x6f38a930`. Owner +0x1c0 blocks changes while locked. A mode request
hook is not proof that a request was accepted: inspect `select` events too.

| Mode | Animation | RGB |
|---|---|---|
| 0 | Normal | white |
| 1 / 2 / 3 | Select | yellow / red / green |
| 4 | Target | white |
| 5 / 6 / 7 | TargetSelect | yellow / red / green |
| 8 | TargetSelect | dynamic `0x6f343e00` lookup |
| 9 | HoldItem | white |
| 10 / 11 / 12 / 13 | Scroll Left / Right / Up / Down | white |
| 14 / 15 / 16 / 17 | Scroll Up Left / Up Right / Down Left / Down Right | white |

The static table at `0x6fabb554` has 18 RGB triplets. Alpha is 255. Its mode-8
triplet `(255,10,32)` is bypassed by the dynamic lookup; do not hardcode it as
mode-8 output. InvalidTarget is registered but is not selected by this switch;
its other producers have not been exhaustively mapped. Mode 9 additionally
resolves the held object and passes its texture to `0x6f1a4c60` with replaceable
slot **0x15**. Normal invokes `0x6f3899d0` to clear associated interaction state.

Selection uses `0x6f1a42b0`: **ECX=sprite, EDX=token-array pointer**, stack
arguments count and flags. The examined cursor calls pass flags **0**. Ghidra's
inferred integer return is unreliable: callers discard an **x87** return.

Uber update virtual slot +0xc is `0x6f1a0010`. Pending sequence value **-2**
invokes `0x6f1a1740`, which resolves the queue and reaches model sequence start
through `183150 -> 8a3a70 -> 8a4eb0`. The model's animation state is at
**model +0x98**; current index is state +0x58, with clock entries at the pointer
state +8, stride **0x10**. State and sequence IDs belong to different layers.

Live observations:

- Human Normal starts at 333; Scroll Right starts at 4833, matching their MDX
  intervals. The selection call does not itself finish the sequence change;
  the update resolves it.
- Animation time advances with the UI/sprite update delta in seconds converted
  to the model's millisecond clock. It is not simply the global simulation time.
- A separate F10-menu capture (99 samples, 265 D3D draws) showed Normal's clock
  continuing to advance while the gameplay menu was open.
- Repeated internal `sequence-switch` events can occur while holding one mode;
  these are not repeated user mode changes. The queue/loop machinery can mark
  the sprite sequence pending again.

The shared sequence system can transfer proportional phase through
`0x6f8a3b80`, but only when **both runtime sequence records** have bit 2 at +0xc.
Do not generalize the observed HumanCursor reset to every animated UI model.
`0x6f8a5000` handles wrapping/clamping and end callbacks. HumanCursor's 14 raw
SEQS records have flags 0; the eight scroll intervals are 200 ms.

The base sprite frame has optional delta clamping (+0x1a4 enable, +0x1a8 limit,
constructor default limit .5). The cursor's constructor leaves that enable
field zero; an unconditional .5-second clamp would be an unsupported inference.

### Sequence completion callback and incoming references

`Sprite_CompleteQueuedSequence` (`6f1a1680`) is registered through
`6f183130`. Its three incoming **data references** are `6f1a30e5`
(sprite clone, containing function `6f1a2f70`), `6f1a494f` (path loader,
`6f1a47b0`), and `6f1a4bc7` (bind existing model, `6f1a4bb0`). All three
containing functions were decompiled; a direct-call-only xref audit misses
this callback. The saved Ghidra name and `MapCursor.java` retain its role.

The callback first consults virtual slots +0x68/+0x6c. For a valid looping
sequence with one queue entry, it sets the signed sequence marker to -2 and
invalidates through virtual +0x20(-1). For a non-looping sequence with one
entry and no queue flag 0x400, it marks the queue head 0x1000000 and returns
without restarting. With multiple entries it removes the head, advances the
ring index modulo capacity, and marks the sequence pending. These are shared
sprite rules; the shipped cursor sequences audited here have looping flags.

## Update-to-draw chain and virtual xrefs

```text
CCursorFrame +2c -> 11d520 position and SpriteFrame_Update (0f73e0)
  -> 11e7e0 collect sprite tree
     -> 1a31c0 attachment traversal
        -> callback 11f1f0 -> 1a2c10 -> Uber +0c -> 1a0010 update
        -> 11ee60 retain eligible sprite in draw buffer

CCursorFrame +30 -> 0f72f0 SpriteFrame_Draw
  -> 11f1d0 reverse buffer traversal -> 1a2bf0 Sprite_Draw
     -> Uber +14 -> 1a54f0 apply dirty color/alpha
     -> 18c810 queue model geosets/effects/children
  -> 18ce80 flush shared draw queues
     -> 18b770 and 18b8e0
        -> queue kind 0 -> 18d760 -> 18d7c0
           -> 18da10 shared-stream path (observed HumanCursor)
              -> 18e280 texture coordinates; 18c5b0 vertex streams
              -> 136dc0 -> device +70 -> 1424e0 upload 16-bit indices
              -> 18deb0 material layers -> 136db0
                 -> device +74 -> 1422b0
                    -> 1431d0 flush cached graphics state
                    -> IDirect3DDevice9 +148 DrawIndexedPrimitive
```

The alternative `0x6f18d800` path iterates layers separately when alpha/UV-stream
sharing is not possible. Do not label either path cursor-specific. All model
effects, materials and animation remain shared renderer responsibilities.

### Observed material and GPU contract

The validated Human01 trace captured **108 cursor samples and 346 actual D3D
draws** across Normal and Scroll Right, with no observer errors. At the COM draw
entry, the observed quad had four vertices and two triangle-list primitives.
The distinct state combination was:

| D3D9 state | Observed value |
|---|---|
| ZENABLE / ZWRITEENABLE | 1 / 0 |
| ZFUNC | LESSEQUAL (4) |
| ALPHABLENDENABLE | 1 |
| SRCBLEND / DESTBLEND | SRCALPHA (5) / INVSRCALPHA (6) |
| ALPHATESTENABLE | 1 |
| ALPHAFUNC / ALPHAREF | GREATEREQUAL (7) / 4 |
| CULLMODE | CW (2) |
| LIGHTING state | 1 |

These are actual device states, not a claim that every state affects the active
shader path. In particular, the LIGHTING value alone does not establish that
the cursor receives world lighting. Enum values were checked against the local
Wine D3D9 headers. A wrapper-entry snapshot produced different cull/depth/light
values because it preceded the state-cache flush; that snapshot is not used.

The archived HumanCursor MDX has four materials. All have material flags 0x20
and one blend-mode-2 layer with alpha 1. The three atlas materials use priority
-20, layer flags 0x21, texture 0 and texture-animation indices 0/1/2. The held
item material uses priority -1, layer flags 1, texture 1 and no texture animation.
Texture 0 is `ui\\Cursor\\HumanCursor.blp`; texture 1 has no filename and
replaceable ID **21**, agreeing with the held-item setter's **0x15** slot.
The atlas therefore also needs authored texture animation, not just animated
vertices or a fixed UV rectangle.

Key indirect edges were verified as data xrefs and at runtime:

| Table/slot | Entry | Target |
|---|---|---|
| Cursor frame +0x2c | 6fa8da40 | 6f11d520 |
| Cursor frame +0x30 | 6fa8da44 | 6f0f72f0 |
| Uber leaf +0x0c | 6fa92930 | 6f1a0010 |
| Uber leaf +0x14 | 6fa92938 | 6f1a54f0 |
| Observed Gx device +0x70 | table 6fa8e1b0 | 6f1424e0 |
| Observed Gx device +0x74 | table 6fa8e1b0 | 6f1422b0 |

The callback at `0x6f11f1f0` initially lacked a Ghidra function definition; it was
created after inspecting the pointer passed by `0x6f11e7e0`. A decompiler-only
call graph would have left a real gap here. Likewise, Uber +0x14 only propagates
tint; the following model call submits geometry.

## Mode producers and restoration

The direct-reference audit of `Cursor_SelectMode` (`6f38a9c0`) contains
25 call sites in 16 functions. The interaction classes below are identified
from their RTTI-labelled vtables, rather than inferred from animation names.

| Producer | Role / resulting mode |
|---|---|
| `6f35fe60` | Camera-scroll update: modes 10–17, with restoration through the mode stack |
| `6f386f50` | Restore the top saved mode when unlocked |
| `6f3931f0`, `6f394d50` | CSelectMode widget/unit hover: Normal 0 or relationship Select 1/2/3 |
| `6f3bed10`, `6f3c3550` | CTargetMode widget/unit hover: Target 4 or relationship TargetSelect 5/6/7 |
| `6f393900` | Inventory interaction: HoldItem 9, with the selected item's icon |
| `6f3a97f0` | CTargetMode entry: select Target 4, or push explicit mode 4 while locked |
| `6f3a9300` | CDragScrollMode entry: unlock, force-push current mode, select Normal, lock |
| `6f3a9370` | CEscMenu entry: force-push current mode, select Normal, expose cursor |
| `6f3a94d0` | CQuestMode entry: push current mode, select Normal |
| `6f3a9680` | CScriptDialogMode entry: push current mode, select Normal |
| `6f3a97a0` | CSignalMode entry: push current mode, select mode 8, lock |
| `6f38fd20` | Cancel/refresh: Normal when unlocked; force-push explicit Normal when locked |
| `6f3719c0`, `6f36f4d0` | Pause/activation-related reset paths: unlock and restore Normal |

CSelectMode dispatch is `6f392e30` (vtable `6fabba60`, slot +0x0c);
CTargetMode dispatch is `6f3be0a0` (`6fab98b4`, slot +0x0c).
Their entry slot is +0x24. The corresponding tables for drag-scroll, signal,
menu, quest and script-dialog are `6fab9914`, `6fab9944`, `6fabdf34`,
`6fac12f0` and `6fac19b8`.

`6f386fa0` pushes a cursor mode onto the WorldFrame's stack. Arguments are
`force`, `explicitMode`, `mode`; absent `explicitMode`, it reads current mode
at +0x1ac. It appends when forced, or when unlocked and different from the
stack top (an empty stack compares as Normal). Capacity/count/data/growth
are +0x1b0/+0x1b4/+0x1b8/+0x1bc. `6f386f50` only pops when unlocked and
nonempty; it reselects the top with the restore flag and then removes it.
Consequently cancellation while a temporary interaction is locked changes
what will be restored, rather than forcibly replacing its visible cursor.

Exit functions are `6f3ae020` (drag-scroll: unlock/pop), `6f3ae0a0`
(menu: pop/refresh/visibility restoration), `6f3ae210` (quest: pop),
`6f3ae2c0` (script dialog: pop), and `6f3ae380` (signal: unlock/pop).
Menu/quest/script-dialog exits also release their pause state.

Mode 8 is **minimap signalling**, not a generic invalid-target color. Its tint
comes through `6f343e00`: player-color mapping `6f3423e0`, then the
relationship/color-mode override `6f33c820`, then table `6fd6a6fc`.
Do not hardcode the unused RGB-table row for this mode.

`InvalidTarget` token 4 is registered, but the audited cursor mode selector
never emits it. Both unit and widget target-validation failures choose
**mode 4 / Target token 2**. Registration alone is not evidence that an
animation is reachable. The first-sprite cursor owners and tokenizer users
were also cross-referenced: creation/load defaults select Normal; unrelated
portrait/button sprite animation calls must not be counted as cursor uses.
This statement applies to the audited 1.27b paths, not to every Warcraft build.

## Per-update clock verification

The isolated Human scene recorded all eight scroll sequences plus Normal,
Select, Target, TargetSelect and HoldItem: **2,490** checked updates,
**793** sequence starts, **765** pending-restart samples and **2,640** draws.
The observer records every update with `--sample-ms 0`. A second trace with
OS process stalls and focus/menu transitions checked **438** updates and
**439** draws. Observed long deltas were 621, 1325 and 2180 ms; none was
clamped to the optional sprite-frame 500 ms limit.

The verified rule is: use the frame returned by `Model_StartSequence` when
selection/restart occurs; otherwise use the previous authored frame. Add the
animation state's integer step at +0x50, then wrap relative to the authored
interval start. At a loop boundary, the end callback marks the sprite's signed
sequence field **-2**. The following update resolves the queue and starts the
sequence again before adding that update's step. The wrapped remainder from
the preceding frame is therefore discarded by the next restart. A single
`(now - initialEpoch) % duration` clock does **not** reproduce this lifecycle.
The observer also records model time +0x4c and sprite time +0xa0; the latter
is not a substitute for the model's integer step.

The validator reads the extracted model's SEQS intervals:

```sh
python tools/frida/trace_wc3_cursor.py --dll "$WC3DATA/game.dll" \
  --pid "$RETAIL_PID" --seconds 45 --sample-ms 0 --output /tmp/cursor-clock.jsonl
python tools/frida/verify_wc3_cursor_trace.py --trace /tmp/cursor-clock.jsonl \
  --model /tmp/HumanCursor.mdx
```

Sparse traces cannot verify this recurrence and are rejected. The validator
also requires a completed trace, observed draws and nonzero checked updates.
Its result establishes the animation-clock recurrence for observed samples;
it does not assert framebuffer equality.

The interaction trace additionally observed green own-unit Select, yellow
allied-unit Select, white rejected self-attack Target, yellow allied
TargetSelect, and red player-0 signal TargetSelect (`ffff0303`). Right-clicking
an inventory icon selected HoldItem and bound replaceable slot **21**. Opening
F10 selected Normal; exit traversed the saved HoldItem restoration before the
subsequent interaction refresh returned to Normal. Do not infer final state
from a single restoration call while ignoring later calls in the same event.

## Isolated retail scene

`tools/frida/make_wc3_cursor_map.py` takes an extracted stock Human01 archive,
reuses its terrain, and replaces its script/player setup. It supports the four
races and a CustomSkin Cursor override; outputs contain retail assets and
should stay outside the repository. Build the current `mpqtool` first:

```sh
make mpqtool
python tools/frida/make_wc3_cursor_map.py --base /tmp/Human01.w3m \
  --race orc --output "$WC3DATA/Maps/CursorAudit-orc.w3m"
```

Launch that map in an isolated retail process, pass the loading screen, then
attach the bounded observer. Archive writer compatibility matters: prior
hash placement and SINGLE_UNIT output prevented these maps from loading;
see [MPQ writer compatibility](../../fs-loading-architecture.md#writer-compatibility-with-classic-storm).

## Partial layouts for future hooks

| Object | Offset | Meaning |
|---|---:|---|
| CScreenFrame | 0x16c | cursor frame pointer |
| CCursorFrame | 0xa8 / 0xac | layer (10000) / alpha (255) |
| CCursorFrame | 0x140 | optional model camera; observed null |
| CCursorFrame | 0x170 / 0x174 / 0x178 | sprite capacity / count / pointer array |
| CCursorFrame | 0x180 | embedded sprite draw buffer |
| CSpriteUber | 0x20 / 0x28 | model pointer / flags |
| CSpriteUber | 0x2c / 0x30 | signed sequence marker / animation set |
| CSpriteUber | 0x34..0x36 / 0x40 | queue count/head/tail bytes / records pointer |
| CSpriteUber | 0xc0 | translation vec3 (track 1) |
| CSpriteUber | 0xe8 | uniform scale |
| CSpriteUber | 0x148 / 0x1b0 | ARGB color / alpha |
| Model | 0x98 | animation-state pointer |
| Gx D3D device | 0x584 | IDirect3DDevice9 pointer |

The frame's draw records are 0x18 bytes and retain sprites until cleared on the
next update. Tree traversal visits negative-order attachments before their
parent, then the remaining attachments; drawing reverses collected records.
Do not assume one sprite equals one model/geoset draw.

## Reproduce the trace

Run retail in a separately owned Wine prefix/display and Frida server. Obtain
the **Frida/Windows PID**, then attach for a bounded interval:

```sh
python tools/frida/trace_wc3_cursor.py \
  --dll "$WC3DATA/game.dll" --remote 127.0.0.1:27045 \
  --pid "$RETAIL_PID" --seconds 45 --output /tmp/wc3-cursor.jsonl
```

The Python environment needs `frida`; this host uses
`/home/lofcz/.local/share/uv/tools/frida-tools/bin/python`. The observer verifies
the supplied DLL hash and target PE timestamp/image size. This identifies the
supported build; it is not a full in-memory integrity verification.

Move the pointer to center and edges, hover units, enter targeting, open F10,
and return. `sample` records position, delta, tint, scale, model-camera pointer,
sequence and authored frame. `select` gives accepted token calls. `draw-path`
records each reached shared renderer stage once. `d3d-draw` records distinct
state combinations **at the actual COM DrawIndexedPrimitive entry**, after Gx's
state flush. `trace-end` reports sample/draw counts; zero counts do not verify
anything. Attaching while loading may produce no cursor samples until play/UI
resumes. The observer detaches without terminating the process.

Instrumentation pitfalls: use register arguments for fastcall; use a fresh
isolated process after an instrumentation crash. An experimental hook inside
the draw wrapper caused a retail crash and was discarded. The reusable probe
hooks function entries and the actual COM method instead. Querying D3D states
at the wrapper entry is too early because cached states have not been flushed.

## Engine clock and framebuffer corrections

The WC3 cursor renderer now advances a retained relative frame from the client
presentation clock (`CL_RealTime`), consumes the current step on mode changes,
and restarts on the update after a wrap. It encodes that relative frame through
the existing sprite animation epoch; other UI sprite clocks are unchanged.
`cl.time` is unsuitable because snapshot parsing resets it to server time;
`tr.viewDef.time` also stops while the paused world is cached.

`renderer_cursor.retail_restart_and_paused_clock` reproduces a frozen world,
a loop crossing, next-update restart, mode changes, and a 1325 ms stall. The
scroll fixture deliberately uses a 273 ms interval. Seven assertions failed
before the clock correction. The client screen test independently verifies
that presentation time reaches `DrawCursor` while world time stays fixed.
Non-looping sequences instead retain the inclusive authored endpoint, as
confirmed in `6f8a5000` and the single-head completion callback. Separate
regressions cover both cursor state and the MDX sprite draw path; four assertions
failed before this correction. Explicit normalized-progress UI samples retain
their existing sampling contract.

The Night Elf framebuffer pair at 1024x768 uses the TFT override MDX and
captured scene pixels as the engine diagnostic background. Correcting the
hotspot by **+0.5 physical pixels in X and top-origin Y** reduced the comparison
from 1,581 differing RGB components / absolute sum 27,578 / maximum 180 to
1,300 components / sum 2,885 / maximum 7. Background pixels outside the cursor
matched. This correction accounts for D3D9 integer pixel centers versus GL
half-integer centers. It is applied only to WC3 cursors using the current UI
scene and drawable dimensions, including high-DPI drawables. Four geometric
regression assertions failed before the correction and passed afterward.

The first capture used D3DFMT_A4R4G4B4 (format 25), while the engine used
full channel precision. A controlled repeat in the isolated Wine prefix set
`HKCU\Software\Blizzard Entertainment\Warcraft III\Video\texcolordepth`
to DWORD 32 before starting retail. The draw probe confirmed D3DFMT_A8R8G8B8
(format 21). At 1024x768, Normal frame 389 then matched **every RGB component**;
the cursor changed 1,532 components between the before/after retail captures,
so this was not an empty draw. Using the base RoC MDX instead of the TFT override
also changes geometry, despite identical sequence intervals.

Eight additional 32-bit captures exercise the actual pointer edges/corners:

| Sequence | Authored frame | Differing RGB components | Sum of absolute errors | Maximum channel error |
|---|---:|---:|---:|---:|
| Scroll Left | 4518 | 9 | 10 | 2 |
| Scroll Right | 4979 | 11 | 15 | 2 |
| Scroll Up | 5216 | 1 | 1 | 1 |
| Scroll Down | 5552 | 0 | 0 | 0 |
| Scroll Up Left | 5785 | 7 | 7 | 1 |
| Scroll Up Right | 6260 | 13 | 13 | 1 |
| Scroll Down Left | 6626 | 10 | 10 | 1 |
| Scroll Down Right | 6957 | 6 | 6 | 1 |

Each comparison covers all 2,359,296 RGB components; the cursor contributes
1,426–1,471 changed components in the retail pair. The residuals are small,
but their exact cause remains unresolved. These captures do not establish
pixel equality for every animation phase, material, race or aspect ratio.

The comparison exposed a diagnostic lifecycle bug: `R_RenderFrame` restores
its previous view for UI scenes, so setting only `viewdef.time` did not carry
`mdxtool --frame` into the subsequent sprite. The tool now encodes requested
time through `drawSprite_t.start_time`, as the cursor does. Replaying the same
captured Scroll Right frame reduced 1,221 differing components / sum 12,035 /
maximum 88 to 11 / 15 / 2. Near the sequence beginning this bug was easy to miss.

`tools/frida/compare_wc3_cursor_capture.py` reproduces the composition and
records the exact render command and metrics in the capture directory. It
requires ImageMagick and a working SDL display; it rejects empty cursor draws,
truncated pixel buffers, mismatched before/after metadata and out-of-range frames.
For example, after extracting the actual retail MDX and its referenced texture:

```sh
DISPLAY=:95 SDL_VIDEODRIVER=x11 python3 tools/frida/compare_wc3_cursor_capture.py \
  --capture /path/to/framebuffer-pair --model /path/to/NightElfCursor-tft.mdx \
  --asset /path/to/NightElfCursor.blp 'UI\Cursor\NightElfCursor.blp'
```

Capture artifacts are in the local analysis project’s `reports/cursor/`,
including `capture-ne-32bit-ready`, `capture-32-{direction}`, and
`directions-32bit-comparison.json`. They are evidence artifacts, not test-suite
inputs; automated regressions use generated model data without retail files.

The Undead ready-state trace independently checked **1,195** frame steps,
**270** starts, **238** pending samples, and **1,274** draws. Its observed set
contains all eight scroll directions, Normal, Select, TargetSelect and HoldItem;
it does not establish coverage of Target for that race.

## Race, targeting, and held-item integration

`UI_UpdateCursorPresentation` resolves `Cursor` through `Theme_PlayerString`:
map `[CustomSkin]` wins, followed by the recipient's race section and the stock
`Default` section (including the existing edition-alias rules). `gi.ModelIndex`
publishes the resolved asset. The shared client receives generic player stats
`UI_PLAYERSTAT_CURSOR_INTERACTIONL`, `UI_PLAYERSTAT_CURSOR_INTERACTION`, and
`UI_PLAYERSTAT_CURSOR_IMAGE`; it does not map race IDs to file paths or interpret
WC3 interaction values. Front-end presentation resolves `Default/Cursor` directly from
`UI/war3skins.txt`. Map-local models use the existing scoped model registration
path. A model change resets the cursor clock; the renderer borrows registered
model handles and never releases a client-owned cursor model.

The server publishes a **pointer interaction**, not a retail cursor mode:
`wc3PointerInteraction_t` contains Idle=0, Targeting=1, Holding=2 and Signaling=3.
`UI_PLAYERSTAT_CURSOR_INTERACTION` transports that opaque interaction. The shared
`drawCursor_t.interaction` adds hover ownership/hostility and edge direction.
`R_ResolveCursorMode` produces the complete `wc3CursorMode_t` (0–17), and the
single `cursor_modes` table supplies the animation and fixed tint. The table
above is the retail mapping; the two enums are deliberately different types.

Owned hover selects green (3/7), hostile hover red (2/6), and allied or neutral
hover yellow (1/5). Ground targeting uses white Target (4). Held items resolve
to 9; all eight edge directions resolve to 10–17. Signaling resolves to 8 and
locks out both hover and scroll substitutions. Command acceptance remains
subject to the existing server ability validators; hover highlighting does
not yet run each ability's complete target predicate.

### Signal overlay and dynamic color

`cmd signal` (default binding **Alt+G**) enters a transient `cursor_signal`
overlay without modifying the underlying menu callbacks or held-item reference.
Esc/right click pops it; a world/entity/minimap left click emits an ally ping
through the existing minimap service and pops it. This preserves the original
target/held-item command. `UI_PLAYERSTAT_CURSOR_FLAGS` carries the generic
`CURSOR_INPUT_MINIMAP_POINT` policy so the shared minimap routes that click as
`point x y`, without starting camera drag or interpreting a WC3 enum.

Signal's cursor image is the skin-resolved `TeamColor` prefix plus the player's
color index modulo skin `TeamColors`, formatted `%s%02u.blp`. World alliance
color filtering uses `TeamColorFilter/ColorIndexPlayer` from the merged misc
configuration; minimap-only filtering retains the player's normal color.
The renderer uses the decoded level-zero **first texel**, including its alpha,
as Signal's tint. `texture_t.first_pixel/has_first_pixel` retain that value at
upload time for both RGBA and BGRA sources, independent of GL/GLES upload
conversion. Higher mip levels do not overwrite it. The signal image is a color
source, not a replaceable-21 override. Missing skin data is diagnosed and
cancels the unusable overlay; a missing decoded image is diagnosed by the
renderer rather than substituting a guessed RGB color.

Fresh Ghidra checks establish this chain:

- `6fab9944 + 0x0c` points to signal event dispatch `6f3be010`;
  click handlers `6f3c3500`, `6f3c41d0`, `6f3be690`, `6f3c21b0` call
  `6f3c4de0`, which reaches `6f26ff00` / `CNetCommandAllyPing`.
- `6f3a97a0` pushes/selects 8/locks; `6f3ae380` unlocks/pops.
- `6f38a930` uses dynamic lookup `6f343e00` for mode 8, including the
  returned alpha; the other 17 modes use fixed RGB and alpha 255.
- Xrefs to palette pointer `6fd6a6fc` lead to loader `6f3535d0`:
  instructions `6f3537b4`–`6f353804` resolve the `TeamColor` skin key,
  decode each image through `6f70dd90`, and copy its first texel.
- `6f3423e0` maps player color and applies the palette-count modulus.
  `6f33c820` applies the color filter; setter `6f33c920` is called by
  `6f36abf0`. Disassembly confirms filter value 2 enables world overrides,
  while values 0/1 retain player colors for this cursor lookup.

The ping itself currently uses the existing one-second, white minimap service
contract. Exact retail ping duration, player tint on the authored ping model,
and the existing HUD signal-button wiring remain **minimap work**, separate
from the now-reachable Signal cursor. Forced team-color policy in special
retail game modes also needs further producer mapping. Do not treat the cursor
mode matrix as proof of those broader minimap behaviors.

HoldItem is white and uses the carried item's authored `Art` at **replaceable
ID 21**. `drawSprite_t.skin/skin_slot` reach `renderEntity_t.skin/skin_slot` and
the MDX texture resolver. Slot zero retains existing model-wide replacement
behavior for other callers; a nonzero slot restricts the override. TeamColor,
TeamGlow and other authored replacement slots remain intact. Scrolling
supersedes targeting/held items temporarily and does not tint or replace the
scroll artwork. Modal input, focus loss, mouse-look and drag-pan temporarily
show Normal; restoring gameplay input restores the server-owned mode.

The item drag retains its spawn generation as well as its edict pointer.
Inventory membership, current ownership/control, drop capability and generation
are rechecked, preventing a recycled slot from supplying an unrelated icon or
receiving an old drop command. Invalidating the source cancels the target callbacks
as well as the icon, so subsequent clicks are not consumed by a stale drag.
Cancel and accepted point drops clear the mode
and image even when the actual world drop waits for a later simulation tick.
Held pointers, generations and target callbacks are transient across save/load;
Signal, input-policy flags and mode/image are cleared and connected clients re-resolve their cursor assets.
Save version **52** rejects the previous client layout.

The transport regression exposed that only selected stat pairs were encoded.
Protocol **14** retains a Quake 2-style independent 32-bit stat-change mask and
16-bit changed values after the normal player fields. All 32 stat slots,
including clearing the cursor image to zero, round-trip without consuming more
player-field mask bits. Clients and servers must use the same protocol version.

Tests cover all four race sections with non-stock paths, map override/removal,
command/cancel and inventory/drop lifecycles, save/load, slot-21 isolation,
modal/drag restoration, model switches, and all-stat delta updates/clears.
Use `make test-wc3-engine WC3_PATTERN='wc3_cursor.*'` for the server regressions
in both RoC and TFT, and `make test-renderer-model test-mdx-ui test-mdx-texture`
for the renderer regressions. `make test` includes the client/transport checks.

## Validation status

The port onto upstream `1b3d21d2` builds with `make build`. Renderer model and
shadow suites each pass 5,754 assertions in 115 tests (including retained
upstream SPN-event tests); MDX UI passes 31 in six tests and replacement-texture
isolation passes six assertions. The shared client/core suite passes 1,780
assertions in 216 tests. Cursor integration passes 72 assertions in four tests
per edition, covering race/skin selection, invalid-item cleanup, signal
restoration over targeting and a real held item, color-filter image selection,
and transient save/load cleanup. The JASS map suite passes 175 assertions in
67 tests per edition, including both retained-group enumeration regressions.

`make test` is **not green** on this host. The crash previously described as
occurring after Way Gate save tests was localized with GDB to the unchanged
`client_input.menu_sdl_input_is_exclusive_with_world_presentation` test at its
`SDL_PushEvent` call. The backtrace enters `/usr/lib/libSDL2-2.0.so.0`, then
`/usr/lib/libSDL3.so.0`. Running that existing test alone reproduces SIGSEGV;
the preceding Way Gate diagnostics do not identify the failing subsystem.

Python cursor tools compile, the Frida JavaScript passes `node --check`, and
`git diff --check` passes. The full 18-mode renderer matrix does not establish
framebuffer parity for every race, custom material, or target predicate. The
isolated retail processes used for the earlier 32-bit captures were shut down
after collection.

## Remaining parity work

This gives reusable renderer and state entry points for targeting, selection,
held items, skin changes and other animated UI. Still separate work:

- Dynamically exercise the statically mapped mode producers and restoration paths.
- Verify every race/CustomSkin asset and held-item replacement path dynamically.
- Match per-ability target eligibility during hover, beyond the current live
  selectable-entity highlight and authoritative validation when clicked.
- Compare framebuffer output across resolutions/aspect ratios and all materials;
  matching a call path does not establish pixel equality.

See also [UI authoring](../../ui-authoring.md),
[UI lifecycle](architecture/ui-flow.md), and [scroll cursors](scroll-cursors.md).
