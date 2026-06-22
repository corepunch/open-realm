# WoW Attack Damage Point from M2 Events

## Problem

Attack hit moment was hardcoded at 35% of animation duration (`g_ai.c:52`),
which makes the hit come too early in the animation. In WoW, damage timing is
encoded as M2 event timestamps inside each creature/player model file — not in
a DBC/SLK spreadsheet like WC3's `dmgpt`.

## Findings

### M2 Event System

Each M2 model has an `events` array in the header. Each event has:
- `eventId` (uint32): event type (4 = weapon_left, 5 = weapon_right for melee hits)
- `data`, `bone`, `position`: metadata
- `eventTrack`: per-animation-sequence timestamps defining WHEN the event fires

The weapon-hit event timestamps are the actual damage point times.

### M2 Header Layout

Classic M2 (version <= 263) and modern M2 have different header layouts.
Classic has extra fields (`playable_animation_lookup`, `texture_flipbooks`,
`views` as m2Array vs uint32) that shift the `events` offset:
- Classic: events at byte 284 from header start
- Modern: events at byte 264 from header start

### Event Struct Sizes

- Classic: 52 bytes (eventId + data + bone + pad + position + svM2TrackClassic_t)
- Modern: 44 bytes (eventId + data + bone + pad + position + svM2Track_t)

### Track Format

- Classic track: `ranges` (m2Range per sequence) + flat `times` array
  - For sequence i: `ranges[i].start` indexes into `times`
- Modern track: `sequence_times` (array of per-sequence timestamp arrays)

## Changes Made

### common/shared.h
- Added `DWORD damage_point` field to `animation_t` struct

### games/world-of-warcraft/game/g_model.c
- Defined M2 event structs (`svM2EventModern_t`, `svM2EventClassic_t`)
- Defined track structs (`svM2Track_t`, `svM2TrackClassic_t`, etc.)
- Added `M2ArrayAt()` helper for reading m2Array data
- Added `M2ReadEventsArray()` to read events at correct header offset
  (accounts for classic vs modern header layout differences)
- Added `M2EventTrackTime()` to extract first timestamp for a sequence
  from an event track
- Updated `LoadModelM2()` to parse events and find weapon-hit timestamps
  (eventId 4/5) per sequence, stored as `damage_point` on animation_t

### games/world-of-warcraft/game/g_ai.c
- Updated `Wow_AttackTimingFromAnimation()` to prefer
  `animation->damage_point` (from M2 events) over the 35% fallback

### tools/m2tool.c
- Added `M2EventIdName()` and `PrintEvents()` for event dumping in `--info`

## TODO / Follow-up

- [ ] Fix m2tool event struct stride: currently events 0-32 show garbage IDs
  for classic M2 (OrcMale v256). Need to verify correct event struct layout
  against actual binary data. Events 33-38 are clean but appear to be padding.
- [ ] Test with a WoW creature model that has clear weapon-hit events in
  attack animations (e.g. Creature/Gnoll/Gnoll.m2)
- [ ] Verify the `M2ReadEventsArray` offsets (284/264) against m2tool's
  sequential header parsing for both classic and modern M2 versions
- [ ] Consider adding event dumping to `--dump-all` mode with per-event
  timestamps per sequence
