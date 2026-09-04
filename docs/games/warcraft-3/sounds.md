# Warcraft III — Unit Sound System

See also: [Sound Architecture](../../architecture/sound.md).

## Sound Catalog Files

Warcraft III sound mappings are primarily driven by SLK tables shipped in `War3.mpq`:

| File | Purpose |
|------|---------|
| `UI/SoundInfo/UnitAckSounds.slk` | Acknowledgement (what/yes/attack/pissed/ready/warcry) sounds per unit |
| `UI/SoundInfo/UnitCombatSounds.slk` | Combat impact/swing sounds by weapon/armor type |
| `UI/SoundInfo/UISounds.slk` | Interface sounds (button clicks, etc.) |
| `UI/SoundInfo/AnimSounds.slk` | Sounds triggered from MDX animation events |

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

Death sounds are **not** in `UnitAckSounds.slk`. They are raw WAV files at `{modelDir}\{ModelName}Death.wav` (e.g. `Units\Human\Footman\FootmanDeath.wav`).

## Sound Events Per Unit (Footman Example)

| Label | Files | Trigger |
|-------|-------|---------|
| `FootmanWhat` | `FootmanWhat1-4.wav` | Click-to-select (acknowledgement) |
| `FootmanYes` | `FootmanYes1-4.wav` | Move order |
| `FootmanYesAttack` | `FootmanYesAttack1-3.wav` | Attack order / swing |
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
```

All three use `UnitAckSounds_t` because they share the `FileNames` /
`DirectoryBase` sound-row schema. `G_UnitAckSound`, `G_UnitCombatSound`, and
`G_UISound` perform exact row-name lookup.

### Unit acknowledgement and completion sounds

`G_RegisterUnitSounds` reads the unit's `usnd` label from `unitUI.slk` and
registers:

- `sound.select[]` <- every `{label}What` file;
- `sound.yes[]` <- every `{label}Yes` file;
- `sound.ready[]` <- every `{label}Ready` file;
- `sound.attack` <- the first `{label}YesAttack` file;
- `sound.death` <- `{label}Death` when present, otherwise the raw
  `{modelDir}\{ModelName}Death.wav` path.

Selection and normal right-click acknowledgements are queued until
`G_RunEntities` clears the previous one-shot queue. They are emitted as
owner-only `svc_sound` packets to the unit owner.

Training completion selects a random registered `Ready` variant and queues it as owner-only `svc_sound`. For unit-source sounds the server resolves the recipient from the unit's WC3 player ownership. For local presentation APIs such as JASS dialogue, the game passes the connected client edict and the server resolves that exact edict before falling back to player ownership. This distinction is required because a campaign's Warcraft player number is not necessarily the engine connection slot.

### Death sounds

`unit_die` queues the already-registered death sound for the next sound packet.
Death is a world event and is not owner-filtered. Queueing is
required because JASS/events execute before `G_RunEntities`, whose first pass
clears the previous snapshot's one-shot fields; writing `s.event` directly from
a scripted `KillUnit` path would otherwise be erased before transmission. The
world-event queue is applied after acknowledgement/owner queues, so death wins
if several one-shots are pending on the same entity.

### Construction completion and UI sounds

`G_CompleteConstruction` resolves the owner's `JobDoneSound` field through
`UI\war3skins.txt`, resolves that alias through `UISounds.slk`, and queues the
chosen authored file as an owner-only `svc_sound` from the completed building. The sound
is therefore positional at the structure and audible only to its owner. The same completion now emits an owner-only minimap/recent-alert notification at the completed structure; training completion and research completion do the same at their resulting unit/producer locations. Alert rendering/history is documented in [alerts-and-minimap-pings.md](alerts-and-minimap-pings.md).

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

### Other implemented UI aliases

The generic `UISounds.slk` resolver is also used for:

- `PlaceBuildingDefault` after a build placement is accepted; this is sent as
  non-positional UI audio to the issuing player.
- `ItemGet` after a world-item pickup succeeds; this is queued as positional
  owner-only `svc_sound` from the carrying unit.
- `ItemDrop` after an inventory item is returned to the world; this is queued
  as positional owner-only `svc_sound` from the dropping unit.

Aliases are resolved from Warcraft data rather than hard-coded WAV paths.

### Combat sounds

`UnitCombatSounds.slk` is currently consumed for lumber harvesting. The
attacker's authored weapon sound (`ucs1`) is combined with `Wood`, for example
`MetalLightChopWood`, and all authored variants are registered. A successful
chop chooses one variant; a lethal chop replaces it with one of
`Sound\Destructibles\TreeFall{1,2,3}.wav`.

Normal weapon-vs-armour impact resolution is still missing. `sound.attack` is
still populated from `YesAttack` and fired on attack swings, which is not the
final WC3 semantic split: `YesAttack` should be an attack-order acknowledgement
while swing/impact audio should come from combat/model sound data. Keep this as
a known gap rather than building additional behavior on `sound.attack`.

### JASS sound handles

`CreateSound` now retains the JASS handle's 0..127 volume, optional fixed world position, and optional attached unit. `StartSound` samples that state into the existing generic `svc_sound` packet: fixed/attached one-shot sounds use `gi.PositionedSound`, local-player calls remain owner-only, and unscoped calls broadcast once rather than once per configured player slot. Attachment currently samples the unit position when playback starts; continuous moving-emitter tracking, stop/fade state, pitch/cone/distance controls, and volume-group mixing remain future work.

The client mixer currently decodes WAV PCM only. Campaign dialogue and thematic music assets authored as MP3 therefore remain unsupported even when registration and recipient routing are correct. Music/thematic-music natives should stay explicit gaps until a compressed-audio/music transport exists.

### Not yet implemented

The following remain separate follow-up work:

- `{label}Pissed` repeated-click responses;
- `{label}Warcry`;
- `AutoCastButtonClick`, `SubGroupSelectionChange`, and rally-point UI sounds;
- `ConstructingBuilding` construction-start/selection sound behavior;
- `YesAttack` as a distinct attack-order acknowledgement;
- general weapon-vs-armour `UnitCombatSounds.slk` impacts;
- ability/buff `EffectSound` and `EffectSoundLooped`;
- MDX `EVTS` -> `AnimLookups.slk` -> `AnimSounds.slk` playback;
- `CreateSoundFromLabel` and remaining JASS sound-handle controls such as stop/fade, pitch, cone, and distance parameters;
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
