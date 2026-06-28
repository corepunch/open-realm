# WoW Attack Damage Point from M2 Events

## Problem

Attack hit moment was hardcoded at 35% of animation duration (`g_ai.c:52`),
which makes the hit come too early in the animation. In WoW, damage timing is
encoded as M2 event timestamps inside each creature/player model file — not in
a DBC/SLK spreadsheet like WC3's `dmgpt`.

## Findings

### M2 Event System

Each M2 model has an `events` array in the header. Each event has:

```c
struct M2Event {
    uint32_t identifier; // 4-char FourCC packed as uint32, e.g. '$SWH', '$SHD'
    uint32_t data;       // SoundEntries ID passed when event fires
    uint32_t bone;       // bone the event is attached to
    C3Vector position;   // position relative to bone (12 bytes)
    M2TrackBase enabled; // timestamp-only track: each timestamp = "fire now"
                         // no values array — firing is implicit at each timestamp
};
```

The `identifier` field stores a 4-byte ASCII string packed little-endian into a
uint32. Match it with e.g. `memcmp(&ev.identifier, "$SWH", 4)` or by casting:
`*(uint32_t*)"$SWH"`. **Do not treat it as a numeric enum.**

Known weapon-hit event identifiers (from wowdev.wiki):

| Identifier | Meaning                          |
|------------|----------------------------------|
| `$SWH`     | Swing weapon hit (melee, right)  |
| `$SHD`     | Shield hit / parry sound         |
| `$SPN`     | Spell/projectile notify          |
| `$SPL`     | Spell start                      |
| `$SPS`     | Spell stop                       |
| `$SPH`     | Spell hit                        |
| `$BWA`     | Bow pull (attach to right hand)  |
| `$BWP`     | Bow projectile                   |
| `$BWR`     | Bow release                      |
| `$BWS`     | Bow string (all $BW* on r.hand)  |
| `$SND`     | Generic sound event              |
| `$FTP`     | Footstep                         |
| `$DST`     | Death / destroy trigger          |

There are also non-`$` identifiers like `DEST`, `POIN`, `WHEE` whose usage is
unclear (wowdev.wiki notes these exist but does not document their semantics).

**For melee damage point: match `$SWH`. Check `$SHD` for off-hand/shield.**

### M2TrackBase Layout (version-dependent)

The `enabled` field in M2Event is an `M2TrackBase`, which is a timestamp-only
track (no values). Its layout changes between pre-Wrath and Wrath+:

```c
// Wrath+ (version >= 264)
struct M2TrackBase {
    uint16_t interpolationType; // always 0 for event tracks
    uint16_t globalSequence;    // -1 if not global
    M2Array<M2Array<uint32_t>> timestamps; // [seq_idx][timestamp_idx]
};

// Pre-Wrath / Classic / BC (version <= 263)
struct M2TrackBase {
    uint16_t interpolationType;
    uint16_t globalSequence;
    M2Array<M2Range>    interpolation_ranges; // [seq_idx] -> {min, max} index into flat times
    M2Array<uint32_t>   timestamps;           // flat array; range[i].minimum is start index
};
```

The M2Event struct itself does **not** change size across versions — it is
always `4 + 4 + 4 + 12 + sizeof(M2TrackBase)` bytes. Only the internal
layout of `M2TrackBase` differs.

Struct sizes:
- Wrath+ event: `4+4+4+12 + (2+2+8+8)` = **44 bytes**
- Pre-Wrath event: `4+4+4+12 + (2+2+8+8)` = **44 bytes** (same! ranges/times
  are both M2Array = 8 bytes each; the difference is in what they point to)

### M2 Header Layout

`events` M2Array is at a fixed offset in the header, but pre-BC headers have
extra fields that shift subsequent arrays:

```
Modern (Wrath+, version >= 264):
  0x100  M2Array<M2Event> events

Classic/BC (version <= 263) — extra fields before events:
  +0x08  playable_animation_lookup  (M2Array, 8 bytes, BC only)
  +0x08  skin_profiles              (M2Array instead of uint32, 8 vs 4 bytes)
  +0x08  texture_flipbooks          (M2Array, BC only)
  => events shift by ~8-24 bytes depending on exact version
```

Verified header offsets for events M2Array:
- **Wrath+ (>= 264)**: `0x100` from file start
- **BC (260-263)**:    `0x118` from file start (approx; verify per file)
- **Classic (<= 257)**: `0x10C` from file start (approx; verify per file)

Use sequential header parsing rather than hardcoded byte offsets — read each
M2Array field in order, skipping version-conditional fields.

### Track Time Extraction

**Wrath+ (per-sequence arrays):**
```c
// events array at header offset, event struct stride = 44 bytes
// timestamps.offset -> array of M2Array<uint32_t>, one per sequence
M2Array seq_array = *(M2Array*)(base + track.timestamps.offset + seq_idx * 8);
if (seq_array.size > 0)
    damage_point = *(uint32_t*)(base + seq_array.offset); // first timestamp
```

**Pre-Wrath (flat array + ranges):**
```c
M2Range range = *(M2Range*)(base + track.ranges.offset + seq_idx * 8);
if (range.maximum >= range.minimum) {
    uint32_t flat_idx = range.minimum;
    damage_point = *(uint32_t*)(base + track.timestamps.offset + flat_idx * 4);
}
```

## Changes Made

### common/shared.h
- Added `DWORD damage_point` field to `animation_t` struct

### games/world-of-warcraft/game/g_model.c
- Defined `svM2Event_t` with `identifier` as `uint32_t` (FourCC, not enum)
- Defined `svM2TrackBase_t` and `svM2TrackClassic_t` for Wrath+ and pre-Wrath
- Added `M2ArrayAt()` helper for reading M2Array entries by index
- Added `M2ReadEventsArray()` — reads events at correct header offset
  using sequential field parsing (not hardcoded byte offsets)
- Added `M2EventTrackTime()` — extracts first timestamp for a sequence
  from an event track, handling both pre-Wrath and Wrath+ layouts
- Updated `LoadModelM2()` to scan events for `$SWH`/`$SHD` identifiers
  per sequence and store first match as `animation->damage_point`

### games/world-of-warcraft/game/g_ai.c
- Updated `Wow_AttackTimingFromAnimation()` to prefer
  `animation->damage_point` over the 35% fallback when non-zero

### tools/m2tool.c
- Added `M2EventIdStr()` to print identifier as 4-char string
- Added `PrintEvents()` for `--info` event dumping with per-sequence timestamps

## TODO / Follow-up

- [ ] Fix m2tool event stride: use sequential header parse, not fixed offset,
  to locate the events M2Array for classic M2 (OrcMale v256). Garbage IDs
  suggest events array is being read at wrong file offset.
- [ ] Test with a creature that has clear `$SWH` events: `Creature/Gnoll/Gnoll.m2`
- [ ] Verify pre-Wrath flat-range extraction: dump `interpolation_ranges` and
  `timestamps` from OrcMale and manually cross-check Attack animation timestamps
- [ ] Consider also checking `$SHD` for shield/off-hand slot damage points
- [ ] Consider adding event dump to `--dump-all` mode with per-sequence
  timestamp table (seq_id, timestamp_ms columns)
- [ ] Audit `M2EventTrackTime()` for the case where `globalSequence != 0xFFFF`:
  global-sequence tracks have only one sub-array (index 0) regardless of
  which animation is playing; guard against seq_idx out of range