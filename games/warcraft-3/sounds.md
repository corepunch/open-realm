# Warcraft III — Unit Sound System

See also: [Sound Architecture](../../architecture/sound.md).

## Sound Catalog Files

Warcraft III sound mappings are primarily driven by SLK tables shipped in `War3.mpq`:

| File | Purpose |
|------|---------|
| `UI/SoundInfo/UnitAckSounds.slk` | Acknowledgement (what/yes/attack/pissed/ready/warcry) sounds per unit |
| `UI/SoundInfo/UnitCombatSounds.slk` | Combat impact/swing sounds by weapon/armor type |
| `UI/SoundInfo/UISounds.slk` | Interface sounds (button clicks, etc.) |
| `UI/SoundInfo/AbilitySounds.slk` | Ability/effect sounds referenced by `Effectsound` aliases |
| `UI/SoundInfo/AmbienceSounds.slk` | Ambient/keyed sound aliases also addressable by JASS labels |
| `UI/SoundInfo/AnimSounds.slk` | Animation-event sound labels resolved by MDX `SND` event objects |
| `UI/SoundInfo/DialogSounds.slk` | Dialogue/voice sound labels used by JASS label constructors |

## Audio Decoding

One-shot sound paths accept mono PCM WAV and MP3 files. MP3 dialogue is decoded by the generic sound cache with vendored
minimp3, then converted to the mixer's S16 / 44.1-kHz mono format; this works in builds without FFmpeg. Failed loads are
cached for the active registration sequence to prevent repeated archive reads when a looping sound is unavailable.
See [Sound Architecture](../../architecture/sound.md) for the client decode path.

## SLK Column Layout

Both `UnitAckSounds.slk` and `UnitCombatSounds.slk` share the same column schema (X1–X19):

| Column | Key | Description |
|--------|-----|-------------|
| X1 | *(row key)* | Sound label, e.g. `"FootmanYesAttack"` |
| X2 | `FileNames` | Comma-separated WAV filenames (random selection) |
| X3 | `DirectoryBase` | Directory prefix, e.g. `"Units\Human\Footman\"` |
| X4 | `Volume` | 0–127 integer volume |
| X5 | `Pitch` | 1.0 = normal |
| X6 | `PitchVariance` | Random pitch variation range |
| X7 | `Priority` | Playback priority |
| X8 | `Channel` | Audio channel (1 = voice, 5 = combat) |
| X9 | `Flags` | `WANT3D`, `RANDOMPITCH`, `NODUPEUSERNAMES`, etc. |
| X10–X12 | `MinDistance`, `MaxDistance`, `DistanceCutoff` | 3D attenuation |
| X19 | `EAXFlags` | EAX reverb preset name |

Full path = `DirectoryBase + filename`, e.g. `Units\Human\Footman\FootmanYesAttack1.wav`.

## Unit Sound Label (`usnd`)

`Units/unitUI.slk`, column X3 (`unitSound`) maps each unit's four-char ID to a sound label:

```
hfoo → "Footman"
hpea → "Peasant"
Hamg → "HeroArchMage"
```

This label is the base for all per-unit sound lookups: `{label}What`, `{label}Yes`, `{label}YesAttack`, `{label}Pissed`, `{label}Ready`, `{label}Warcry`.

Death sounds are looked up in `UnitAckSounds.slk` first. When no entry exists, the game checks for raw WAV files beside the model: `{modelDir}\{ModelName}Death1.wav`, then `{modelDir}\{ModelName}Death.wav`. The archive has both naming patterns: `OgreDeath1.wav` and `FootmanDeath.wav`. Some units have neither, so no death sound is registered. Probe the archive before calling `SoundIndex` to avoid publishing a guessed missing path.

## Sound Events Per Unit (Footman Example)

| Label | Files | Trigger |
|-------|-------|---------|
| `FootmanWhat` | `FootmanWhat1-4.wav` | Click-to-select (acknowledgement) |
| `FootmanYes` | `FootmanYes1-4.wav` | Move order |
| `FootmanYesAttack` | `FootmanYesAttack1-3.wav` | Attack-order acknowledgement |
| `FootmanPissed` | `FootmanPissed1-4.wav` | Repeated clicks (idle taunts) |
| `FootmanReady` | `FootmanReady1.wav` | Unit created / train complete |
| `FootmanWarcry` | `FootmanWarcry1.wav` | Special (not commonly triggered) |
| *(raw file)* | `FootmanDeath.wav` | Unit death |

## Combat Sounds

`UnitCombatSounds.slk` maps weapon/armor type combinations to hit sounds.
The unit's weapon type column (`ucs1`/`ucs2` in `UnitWeapons.slk`) and armor type (`udty` in `unitUI.slk`) select the sound set.
Examples:

| Label | Use |
|-------|-----|
| `MetalHeavyBashEthereal` | Ethereal hit |
| `AxeMediumChopWood` | Wood-chop by medium axe |

## Building Sounds

Buildings use `BuildingSoundLabel` (from `*UnitFunc.txt`) which maps to looping construction sounds. Movement sounds use `MovementSoundLabel`.

## OpenWarcraft3 Implementation

### Loaded sound tables

The typed WC3 metadata registry loads these sound tables at `InitUnitData` time:

```text
UI\SoundInfo\UnitAckSounds.slk
UI\SoundInfo\UnitCombatSounds.slk
UI\SoundInfo\UISounds.slk
UI\SoundInfo\AbilitySounds.slk
UI\SoundInfo\AmbienceSounds.slk
UI\SoundInfo\AnimSounds.slk
UI\SoundInfo\DialogSounds.slk
```

All seven use `UnitAckSounds_t` because they share the `FileNames` /
`DirectoryBase` sound-row schema. Typed access remains available for each
catalog. `G_KeyedSound` first preserves the Warsmash-style Ability -> Ambience
-> UI precedence, then falls back through Anim, Dialog, UnitAck, and
UnitCombat labels for Warcraft JASS label constructors.

### Unit acknowledgement and completion sounds

`G_RegisterUnitSounds` reads the unit's `usnd` label from `unitUI.slk` and
registers:

- `sound.select[]` <- every `{label}What` file;
- `sound.yes[]` <- every `{label}Yes` file;
- `sound.ready[]` <- every `{label}Ready` file;
- `sound.death` <- `{label}Death` when present, otherwise the raw
  `{modelDir}\{ModelName}Death.wav` path.

Selection and normal right-click acknowledgements are queued until
`G_RunEntities` clears the previous one-shot queue. They are emitted as
owner-only `svc_sound` packets to the unit owner. Repeated selection keeps a
small presentation-only counter per player/unit: the first three responses use
`What`, then the authored `Pissed` variants are walked in order. Changing the
selection or issuing an order resets that sequence. Selecting an active
construction site uses the owner's `ConstructingBuilding` skin alias instead.

Each registered Warcraft response also caches its decoded-file duration. A unit
with an accepted response still inside that duration rejects another
selection/order acknowledgement, matching Warsmash's per-unit response lock.
The focused selected unit uses `Portrait Talk` for the same lifetime and returns
to `Portrait` when the response expires. This timing is presentation-only state
and does not extend the saved/networked unit contract.

Attack commands resolve a random `{label}YesAttack` response. They no longer
reuse that voice line as a weapon-swing sound; ordinary `Yes` remains the
fallback when a unit has no `YesAttack` row.

Training completion selects a random registered `Ready` variant and queues it as owner-only `svc_sound`. Registered SLK-backed unit responses retain the authored 0..127 row volume and send that volume with their one-shot packet. For unit-source sounds the server resolves the recipient from the unit's WC3 player ownership. For local presentation APIs such as JASS dialogue, the game passes the connected client edict and the server resolves that exact edict before falling back to player ownership. This distinction is required because a campaign's Warcraft player number is not necessarily the engine connection slot.

The ownership lookup must compare against the connected edict's published `client->ps.number`, as snapshot
visibility already does. `server/client.playernum` is lobby assignment state and can remain unassigned or differ
from the game identity. This caused silent selection/move responses in NightElfX01 (game player 0), and also
reproduced in NightElf01 (game player 1): `SV_StartSound: recipient unavailable` appeared even though the response
file, volume, and command queue were valid. Music uses a separate transport and continued playing.
`server_net.unit_ack_uses_game_player_identity_after_campaign_begin` covers conflicting identities, reliable
packet delivery, and rejecting disconnected/unbound recipients; `wc3_unit.smart_move_emits_selected_unit_response`
covers the real SmartPoint command and entity scheduler.

Entity-relative sound packets pack a 13-bit entity number and a 3-bit channel into an **unsigned** 16-bit value.
Sign-extending `MSG_ReadShort` before shifting rejected campaign entities 4096–8191 (NightElfX01 reported
`bad entity=-3935` for entity 4257). `client_sound.packed_entity_above_4095_reaches_mixer` exercises the actual
client decoder through mixer channel creation, including the 4095/4096 boundary, 4257, and 8191. These fixes
do not change the wire format or save layout. Run `make test-server-net test-wc3-engine`; the latter includes
the client packet regression in the engine test executable. Retail comparison commands are in
[the parity harness](../../tools/parity/README.md#linux-retail-comparison-with-wine).

### Death sounds

`unit_die` queues the already-registered death sound for the next sound packet.
Death is a world event and is not owner-filtered. Queueing is
required because JASS/events execute before `G_RunEntities`, whose first pass
clears the previous snapshot's one-shot fields; writing `s.event` directly from
a scripted `KillUnit` path would otherwise be erased before transmission. The
world-event queue is applied after acknowledgement/owner queues, so death wins
if several one-shots are pending on the same entity.

### Construction loops, completion, and UI sounds

When a building enters the explicit construction state, OpenRealm resolves its
`BuildingSoundLabel` through the keyed sound tables and places the resulting
sound index in `entityState_t.sound`. The existing generic snapshot loop mixer
therefore owns start/restart/movement/removal behavior without a WC3-specific
client channel. Completion, cancellation, and construction-site death clear the
loop; the legacy self-linked construction fallback does the same. Authored
construction fade-in/fade-out and loop volume are not represented by the current
snapshot sound field and remain follow-up work.

`G_CompleteConstruction` resolves the owner's `JobDoneSound` field through
`UI\war3skins.txt`, resolves that alias through `UISounds.slk`, and queues the
chosen authored file as an owner-only `svc_sound` from the completed building. The sound
is therefore positional at the structure and audible only to its owner. The same completion now emits an owner-only minimap/recent-alert notification at the completed structure; training completion and research completion do the same at their resulting unit/producer locations. Alert rendering/history is documented in [alerts-and-minimap-pings.md](../../docs/games/warcraft-3/alerts-and-minimap-pings.md).

Immediate UI sounds are sent only when the owning game client is connected.
Reserved/disconnected player slots may already have simulation state but do not yet
have an initialized server message buffer; queued sound packets can remain pending,
while direct owner-only sound presentation waits for a connected client.

Command errors use the same authoritative data chain as Warsmash:

```text
known WC3 command error key
    -> <key>Sound in the local player's war3skins race section
    -> InterfaceError when no dedicated skin field exists
    -> UISounds.slk alias
    -> one random FileNames entry
    -> targeted owner-only `svc_sound` packet to that player
```

SLK-backed immediate/queued UI sounds use their authored row volume when sent.
The currently normalized hard-coded gameplay messages map to these external
keys:

| Message | WC3 key |
|---|---|
| `Not enough food` | `Nofood` |
| `Not enough gold` | `Nogold` |
| `Not enough lumber` | `Nolumber` |
| `Not enough mana` | `Nomana` |
| `Spell is not ready yet.` | `Cooldown` |
| `Unable to build there.` | `Cantplace` |
| `Unable to build so close to the gold mine.` | `Tooclosetomine` |
| `Inventory is full.` | `Inventoryfull` |

`G_ShowCommandErrorText` keeps the existing text presentation and adds the
race/UI sound lookup for those known messages. Other command failures are not
guessed into unrelated WC3 keys; they receive the generic `InterfaceError`
sound, matching Warsmash's fallback behavior.

For targeted spells, mana/cooldown validation stays at the actual cast attempt
(the unit/point selection callback), not the command-button click. This preserves
target-selection lifecycle while still emitting `Nomana` / `Cooldown` feedback
when the player attempts to commit the spell. No-target spells validate and emit
the same feedback immediately because the button click is the cast attempt.

### Ability/effect sounds

Ability and buff/effect UI objects may author an `Effectsound` alias. OpenRealm
resolves that alias from the requested rawcode (falling back to its base
ability/buff object), then resolves the sound row through `AbilitySounds.slk`
with `UISounds.slk` as the same keyed-table fallback used by Warsmash. One-shot
ability sounds are emitted at the authored world/effect point using the sound
row's 0-127 volume.

Blizzard uses this shared path for each DataC shard: the level's authored
`EfctID` supplies both `EffectArt` and `Effectsound`, matching Warsmash's
Archmage implementation. The server deliberately selects the first authored
file variant rather than consuming simulation `rand()`; client-side variant
randomization and authored pitch/pitch-variance remain future audio presentation
work.

### Other implemented UI aliases

The generic `UISounds.slk` resolver is also used for:

- `SubGroupSelectionChange` after `Tab` successfully moves a WC3 multiselect to
  another unit-type subgroup; this is sent as non-positional UI audio to the
  issuing player.
- `AutoCastButtonClick` when an autocast toggle is accepted and `RallyPointPlace`
  when a rally command is accepted; both use owner-only non-positional UI audio.
- `PlaceBuildingDefault` after a build placement is accepted; this is sent as
  non-positional UI audio to the issuing player.
- `ItemGet` after a world-item pickup succeeds; this is queued as positional
  owner-only `svc_sound` from the carrying unit.
- `ItemDrop` after an inventory item is returned to the world; this is queued
  as positional owner-only `svc_sound` from the dropping unit.

Aliases are resolved from Warcraft data rather than hard-coded WAV paths.

### Combat sounds

`UnitCombatSounds.slk` is consumed for both lumber harvesting and ordinary
attack impacts. The attacker's authored weapon sound (`ucs1`) is combined with
the target material suffix (`Flesh`, `Metal`, `Wood`, `Ethereal`, or `Stone`)
and the matching row supplies a random impact variant and authored 0..127
volume. `UnitUI.slk` and `DestructableData.slk` author those armor values as
strings, so metadata decoding preserves the token and normalizes it to the JASS
armor-type integer used by object-data overrides and runtime lookup.

Lumber harvesting continues to use `{weaponSound}Wood` (for example
`MetalLightChopWood`), with a lethal chop replacing it with one of
`Sound\Destructibles\TreeFall{1,2,3}.wav`. `YesAttack` is now reserved for
attack-order acknowledgement rather than attack-swing playback.

### MDX animation sound events

The WC3 renderer consumes model `EVTS` objects whose names begin with `SND`.
Classic event IDs resolve through `UI\SoundInfo\AnimLookups.slk` to a
`SoundLabel`, then through `AnimSounds.slk`; direct `AnimSounds` lookup remains
a fallback for data that already names the sound row. When an entity animation
crosses an authored event key, the renderer chooses an authored file variant
with presentation-local hashing and plays it once at the animated event node's
world position. Event nodes participate in the same MDX node hierarchy used by
attachments and particles, and global-sequence event tracks use the render
clock rather than the entity animation frame.

This renderer path intentionally does not consume gameplay `rand()`, does not
emit a network sound packet, and processes off-screen (but client-visible)
entities before frustum culling. `SND` now shares the MDX event-key dispatcher
with `SPN` child-model events; see [MDX Event Objects](mdx-event-objects.md).
Presentation events are skipped during the shadow-map pass so one animation key
cannot play/spawn twice in a shadow-enabled frame. The generic renderer import
currently carries the resolved path, world position, and authored volume. `Pitch`,
`PitchVariance`, `MinDistance`, `MaxDistance`, and `DistanceCutoff` are parsed
from `AnimSounds.slk` but remain mixer/API fidelity work.

### JASS sound handles

`CreateSoundFromLabel` resolves Warcraft sound labels from the shared sound
catalogs. It preserves the Warsmash-style Ability -> Ambience -> UI precedence
and then falls back through Anim, Dialog, UnitAck, and UnitCombat rows, stores
the first authored file on a normal game-owned sound handle, and initializes
the authored volume. `CreateSoundFilenameWithLabel` keeps its explicit filename
and applies the supported parameters from the label. `SetSoundParamsFromLabel`
likewise keeps the handle's current filename and reapplies supported label
parameters instead of replacing the asset. The current mixer/packet path can
faithfully apply authored volume here; pitch, channel, distance, cone, and fade
parameters remain separate gaps. A missing label leaves `CreateSoundFromLabel`
silent and leaves an existing filename handle otherwise unchanged.

`CreateSound` now retains the JASS handle's 0..127 volume, optional fixed world position, and optional attached unit. `StartSound` samples that state into the existing generic `svc_sound` packet: fixed/attached one-shot sounds use `gi.PositionedSound`, local-player calls remain owner-only, and unscoped calls broadcast once rather than once per configured player slot. Attachment currently samples the unit position when playback starts; continuous moving-emitter tracking, stop/fade state, pitch/cone/distance controls, and volume-group mixing remain future work.

The client mixer currently decodes WAV PCM only. Campaign dialogue and thematic music assets authored as MP3 therefore remain unsupported even when registration and recipient routing are correct. Music/thematic-music natives should stay explicit gaps until a compressed-audio/music transport exists.

### Not yet implemented

The following remain separate follow-up work:

- `{label}Warcry`;
- movement sound-label playback (`MovementSoundLabel`) and exact construction loop fade/volume semantics;
- broader ability/buff `EffectSoundLooped` ownership beyond the existing owned area/channel-effect lifecycle (one-shot `EffectSound` is implemented and current owned effects clear their snapshot loop on destruction);
- remaining JASS sound-handle controls such as stop/fade, pitch, channel, cone, and distance parameters;
- volume-group mixing and music/thematic-music natives;
- MP3 decoding for campaign speech/music assets;
- race alerts such as `UnderAttack`, `GoldMineLow`, and hero death. Research
  completion now resolves the active race skin's `ResearchComplete` alias; the
  remaining race-alert families are still incomplete.

### Client/server flow

`CL_PrepRefresh` registers populated `CS_SOUNDS` entries through
`S_RegisterSound`. Later configstring additions are registered by
`CL_ParseConfigString`, so UI aliases first encountered during gameplay may
still call `gi.SoundIndex` safely. Sound packets use `S_PlaySoundPacket` for
both entity-relative and non-positional playback.

Classic WC3 unit WAVs are multi-sector, encrypted MPQ entries. Their first
sector may use zlib while later sectors use Blizzard adaptive Huffman plus mono
ADPCM (`0x41`). The in-tree MPQ reader must decode every sector before the
sound cache parses the WAV; accepting a partial MPQ read produces a valid
4096-byte RIFF prefix and audibly truncates response lines.

### Audio arbitration investigation (2026-09-25)

Public references inspected:

- [TinkerWorX/warcraftIII](https://github.com/TinkerWorX/warcraftIII), revision
  `e94a800fb99dea45d1e63f3ae1ed9533fca3b15a`: recovered CUnit, CAgent,
  CWidget layouts/vtables and IDA utilities. Useful for navigating unit callers;
  this is not a recovered sound scheduler or a ready-made Ghidra audio database.
- [Warsmash UnitSound.java](https://github.com/Retera/WarsmashModEngine/blob/f9e0aeed4be372d6016519d0e97b384aa873f374/core/src/com/etheller/warsmash/viewer5/handlers/w3x/UnitSound.java):
  `playUnitResponse` gates responses using a per-RenderUnit wall-clock deadline,
  updated after successful playback using decoded sound duration. Its SLK loader
  consumes volume, pitch, distance and loop fields, but not Priority. MeleeUI
  advances from three What responses to sequential Pissed responses. These are
  emulator choices, not proof of retail arbitration.
- [binanana](https://github.com/wowemulation-dev/binanana): modern Ghidra/PyGhidra
  RTTI, string and cross-version analysis tooling for Classic **World of Warcraft**.
  Its workflow is relevant; its client types and offsets are not WC3 types.

Before the fixes below, local code inspection confirmed two separate gaps:
`sound/s_sound.c:S_StartSound` chose the first inactive channel and silently
returned when none was available, without priority comparison or eviction.
`g_commands.c:G_QueueUnitResponseSound` rejected an active response on the same
entity using `G_Time()`, without arbitration across entities. Different units'
barks could therefore overlap.
The latter gate dates to commit `20efbdeab` (git blame); do not infer a global
voice policy from the existing per-unit portrait deadline.

The locally installed retail `game.dll`, SHA256
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`, provides
better anchors for recovering the actual algorithm: RTTI names `CSoundWar3`,
`SoundDBChannel`, `CSoundListener`; strings `SetSoundChannel`, `SoundChannels`,
`SoundManagerLog.txt`, and source paths ending in `CSoundManager.cpp`,
`CSoundManagerI.cpp`, `CSoundManagerI.h`. Its import table includes Miles
`mss32.dll` sample allocation, status, stop/end and end-of-sample callbacks for
2D and 3D playback. These anchors establish a route into the code, not recovered
field offsets or priority semantics. Reproduce with:

```sh
sha256sum "$WC3DATA/game.dll"
strings "$WC3DATA/game.dll" | rg 'CSound|SoundDBChannel|SoundManager|SoundChannel'
objdump -p "$WC3DATA/game.dll" | rg 'AIL_|mss32'
```

The completed Ghidra analysis and focused xrefs recovered the admission policy
and response cooldown. See [retail audio analysis](../../docs/games/warcraft-3/audio-retail-analysis.md)
for version-specific function names, layouts and evidence.

### Authored admission and response timing

Sound-row registration preserves `Channel`, admission `Flags`, and unsigned
`Priority` together. `SCALEPRIORITY` adds the selected variant index. WC3 owns
the sixteen default channel budgets recovered from `6fab5ec8`; the shared mixer
receives a generic policy and does not read Warcraft tables or branch on games.

`G_PlaySound` sends the policy through `gi.SoundPolicy` / `SV_StartSoundPolicy`.
Protocol 11's `SND_POLICY` payload follows optional volume/attenuation/offset:
32-bit priority, 32-bit user identity, 16-bit flags, 16-bit cooldown milliseconds,
then byte channel group/channel cap/global cap/filename cap. Entity identity is
independent of spatial delivery, so owner-local responses remain non-positional.
No entity or player snapshot structure changed. Raw sounds without a registered
row retain the ordinary API; music and movies remain separate PCM streams.

The mixer has 24 slots. Authored requests check duplicate identity and filename,
then channel/global capacity in the recovered order. Greater priority alone does
not authorize replacement. Channel preemption requires strictly lower priority;
oldest-channel preemption permits equality. Global priority preemption permits
equality, while global-oldest preemption does not compare priority. Equal-priority
channel heads are newest first; equal heads across channels use channel order.
The four-instance filename cap has a separate oldest-preemption rule.
`IGNOREUSERNAME` excludes existing instances from duplicate-user lookup and
user preemption, without bypassing an incoming duplicate check. A sample that
ends exactly at a mix-block boundary releases its channel in that callback.

The player-wide response lock has been removed. Owner response packets now carry
an opaque request ID, and the mixer returns reliable `sound_event user request
event` receipts. `SOUND_ACCEPTED` commits the What/Pissed count and that owner's
last accepted label variant. Rejection discards the pending request without
committing either. `SOUND_STARTED` switches the portrait to talking only when
the mixer consumes the first sample, after any requested delay. `SOUND_ENDED`
clears that voice on completion, preemption or stop; another overlapping admitted
voice can still keep the same unit talking. There is no duration deadline or
`GetRealTime` import for portrait prediction.

The game checks connection ownership, unit spawn identity, request identity and
legal event order. A selection generation prevents a delayed admission from
advancing a newly reset selection sequence. At most one unacknowledged request
is retained per unit; an active admitted voice does not impose a guessed server
gate. The client decides duplicates and cooldowns. Freeing a unit, map/save-load
reset and shutdown discard request records; serials continue across resets so
late receipts cannot match a new request. These records are runtime-only.

The mixer reserves notification storage on the main thread before submission;
the audio callback only appends under the device lock. The client drains on its
main thread into the reliable command buffer and retains events if that buffer
is full. Failed loads and unavailable devices return rejection too. Authored
responses retain the 250 ms mixer cooldown beginning at actual completion or
preemption, independent of server time and pause.

Protocol **12** adds the request ID to `soundPolicy_t`; peers need matching
builds. Unit response/Ready/combat caches now use 16-bit sound indices and the
pending index is an int: the former byte storage truncated configstring indices
above 255 and could prevent the packet from matching its request. These are
private game edict fields, not entity/player snapshot extensions. Save version
**46** rejects earlier edict layouts.

SLK variants now register through `gi.SoundIndexAlias(path, label#variant)`.
The server allocates distinct indices for distinct aliases, preserving each
label's policy and volume. Configstrings still contain the original filename,
so the client shares decoded samples and filename duplicate admission remains
shared. Raw `SoundIndex(path)` registrations have an empty alias and cannot
overwrite the authored metadata. Map teardown clears the server alias registry.
The SludgeMonster What/Ready pair was observed using the same WAV on channels
1 and 4 in the retail process; a non-stock fixture reproduced the old overwrite
and verifies independent priority, channel, volume and repeat registration.

Known remaining limits: response random choice is still made on the server.
Simultaneous requests for the same label from different units can be chosen
before the first acceptance receipt arrives; this does not establish exact retail
RNG sequencing. JASS per-handle policy overrides, pitch, distance-based admission and dynamic
channel configuration remain separate work. These changes implement the traced
admission/cooldown rules, not complete retail sound-system parity.

Regression tests cover three concurrent channel-1 voices, duplicate users/files,
permitted/forbidden preemption, equal-priority rules, the 24-slot global limit,
filename cap, actual completion/preemption cooldowns, clock wrap/map teardown,
independent pending requests, receipt-owned portraits, stale/foreign receipts, delayed starts, authored non-stock
priority 1731, and server/client policy transport including unsigned high bits.
The original mixer and unit-clock failures were reproduced before their fixes.
