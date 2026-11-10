# Unit testing cookbook

Practical command-line recipes for spawning and inspecting units. All commands require `sv_cheats 1`.
For full command reference see [cheat-commands.md](cheat-commands.md). For test map descriptions
and make shortcuts see [rendering-scene-workflow.md](rendering-scene-workflow.md).

## StarCraft II

SC2 unit types are catalog identifiers resolved by `SC2_MapResolveUnit`. Case-sensitive.

### Terran

```bash
# Basic infantry
opensc2 -data data/StarCraft2 +set sv_cheats 1 +map Maps/TerranTest.SC2Components \
  +spawn Marine +spawn Marine +spawn Marauder +spawn Ghost

# Mech: tanks and thors
opensc2 -data data/StarCraft2 +set sv_cheats 1 +map Maps/TerranTest.SC2Components \
  +spawn SiegeTank +spawn SiegeTank +spawn SiegeTankSieged +spawn Thor

# Air fleet
opensc2 -data data/StarCraft2 +set sv_cheats 1 +map Maps/TerranTest.SC2Components \
  +spawn VikingFighter +spawn Medivac +spawn Banshee +spawn Battlecruiser

# Full army composition
opensc2 -data data/StarCraft2 +set sv_cheats 1 +map Maps/TerranTest.SC2Components \
  +spawn Marine +spawn Marine +spawn Marine +spawn Marauder +spawn Marauder \
  +spawn SiegeTank +spawn SiegeTank +spawn Medivac
```

Terran unit types:

| Unit           | Catalog ID          |
|----------------|---------------------|
| SCV            | `SCV`               |
| Marine         | `Marine`            |
| Marauder       | `Marauder`          |
| Reaper         | `Reaper`            |
| Ghost          | `Ghost`             |
| Hellion        | `Hellion`           |
| Siege Tank     | `SiegeTank`         |
| Siege Tank (sieged) | `SiegeTankSieged` |
| Thor           | `Thor`              |
| Viking (air)   | `VikingFighter`     |
| Viking (ground)| `VikingAssault`     |
| Medivac        | `Medivac`           |
| Banshee        | `Banshee`           |
| Battlecruiser  | `Battlecruiser`     |
| Raven          | `Raven`             |

### Protoss

```bash
# Gateway units
opensc2 -data data/StarCraft2 +set sv_cheats 1 +map Maps/ProtossTest.SC2Components \
  +spawn Zealot +spawn Stalker +spawn HighTemplar +spawn DarkTemplar

# Robo bay
opensc2 -data data/StarCraft2 +set sv_cheats 1 +map Maps/ProtossTest.SC2Components \
  +spawn Immortal +spawn Colossus +spawn Observer +spawn WarpPrism

# Stargate fleet
opensc2 -data data/StarCraft2 +set sv_cheats 1 +map Maps/ProtossTest.SC2Components \
  +spawn Phoenix +spawn VoidRay +spawn Carrier +spawn Mothership

# Deathball
opensc2 -data data/StarCraft2 +set sv_cheats 1 +map Maps/ProtossTest.SC2Components \
  +spawn Zealot +spawn Zealot +spawn Stalker +spawn Stalker \
  +spawn Colossus +spawn Colossus +spawn HighTemplar
```

Protoss unit types:

| Unit           | Catalog ID          |
|----------------|---------------------|
| Probe          | `Probe`             |
| Zealot         | `Zealot`            |
| Stalker        | `Stalker`           |
| Sentry         | `Sentry`            |
| High Templar   | `HighTemplar`       |
| Dark Templar   | `DarkTemplar`       |
| Archon         | `Archon`            |
| Immortal       | `Immortal`          |
| Colossus       | `Colossus`          |
| Observer       | `Observer`          |
| Warp Prism     | `WarpPrism`         |
| Phoenix        | `Phoenix`           |
| Void Ray       | `VoidRay`           |
| Carrier        | `Carrier`           |
| Mothership     | `Mothership`        |

### Zerg

```bash
# Ling/bane swarm
opensc2 -data data/StarCraft2 +set sv_cheats 1 +map Maps/ZergTest.SC2Components \
  +spawn Zergling +spawn Zergling +spawn Zergling +spawn Zergling \
  +spawn Baneling +spawn Baneling

# Roach/hydra
opensc2 -data data/StarCraft2 +set sv_cheats 1 +map Maps/ZergTest.SC2Components \
  +spawn Roach +spawn Roach +spawn Hydralisk +spawn Hydralisk

# Hive tech
opensc2 -data data/StarCraft2 +set sv_cheats 1 +map Maps/ZergTest.SC2Components \
  +spawn Ultralisk +spawn BroodLord +spawn Infestor +spawn Corruptor

# Mutalisk harass
opensc2 -data data/StarCraft2 +set sv_cheats 1 +map Maps/ZergTest.SC2Components \
  +spawn Mutalisk +spawn Mutalisk +spawn Mutalisk +spawn Mutalisk
```

Zerg unit types:

| Unit           | Catalog ID          |
|----------------|---------------------|
| Drone          | `Drone`             |
| Larva          | `Larva`             |
| Overlord       | `Overlord`          |
| Zergling       | `Zergling`          |
| Baneling       | `Baneling`          |
| Roach          | `Roach`             |
| Hydralisk      | `Hydralisk`         |
| Mutalisk       | `Mutalisk`          |
| Corruptor      | `Corruptor`         |
| Infestor       | `Infestor`          |
| Ultralisk      | `Ultralisk`         |
| Brood Lord     | `BroodLord`         |
| Overseer       | `Overseer`          |
| Queen          | `Queen`             |

### Spawning at specific positions

```bash
# Place two siege tanks at known coordinates
spawn SiegeTank 1 30 50
spawn SiegeTankSieged 1 40 50

# Spawn enemy Zealots (player 2)
spawn Zealot 2 60 60
spawn Zealot 2 62 60
```

### SC2 headless screenshots

```bash
# Terran army screenshot without showing the window
opensc2 -data data/StarCraft2 +set sv_cheats 1 +map Maps/TerranTest.SC2Components \
  +spawn SiegeTank +spawn Marine +spawn Marine \
  +vid_hidden 1 +screenshot 5 +com_frame_limit 10

# Or use the make target
make run-sc2-terran ARGS="+set sv_cheats 1 +spawn SiegeTank +spawn SiegeTankSieged +vid_hidden 1 +screenshot 5 +com_frame_limit 10"
```

## Warcraft III

WC3 units use FOURCC rawcodes (four ASCII characters). The `give unit` command uses the full
CreateUnit lifecycle; `debugspawn` uses the simpler `SP_SpawnAtLocation`.

### Human

```bash
# Basic human army
openwarcraft3 -data 'data/Warcraft III' +set sv_cheats 1 \
  +map 'Maps/(2)Rivercross.w3m' \
  +give unit hfoo +give unit hfoo +give unit hkni +give unit hrif

# Paladin hero with Holy Light and Divine Shield
openwarcraft3 -data 'data/Warcraft III' +set sv_cheats 1 \
  +map 'Maps/(2)Rivercross.w3m' \
  +give unit Hpal AHhb AHds

# Archmage with Blizzard and Water Elemental
openwarcraft3 -data 'data/Warcraft III' +set sv_cheats 1 \
  +map 'Maps/(2)Rivercross.w3m' \
  +give unit Hamg AHbz AHwe

# Mountain King with Thunder Clap and Storm Bolt
openwarcraft3 -data 'data/Warcraft III' +set sv_cheats 1 \
  +map 'Maps/(2)Rivercross.w3m' \
  +give unit Hmkg AHtb AHtc
```

Human units:

| Unit              | Rawcode | Notes             |
|-------------------|---------|-------------------|
| Peasant           | `hpea`  |                   |
| Footman           | `hfoo`  |                   |
| Rifleman          | `hrif`  |                   |
| Knight            | `hkni`  |                   |
| Priest            | `hmpr`  |                   |
| Sorceress         | `hsor`  |                   |
| Mortar Team       | `hmtm`  |                   |
| Siege Engine      | `hmtt`  |                   |
| Gryphon Rider     | `hgry`  |                   |
| Dragonhawk Rider  | `hdhw`  | TFT               |
| Spell Breaker     | `hspt`  | TFT               |
| Paladin           | `Hpal`  | Hero              |
| Archmage          | `Hamg`  | Hero              |
| Mountain King     | `Hmkg`  | Hero              |
| Blood Mage        | `Hblm`  | Hero, TFT         |

Human hero abilities:

| Ability           | Rawcode | Hero        |
|-------------------|---------|-------------|
| Holy Light        | `AHhb`  | Paladin     |
| Divine Shield     | `AHds`  | Paladin     |
| Devotion Aura     | `AHad`  | Paladin     |
| Resurrection      | `AHre`  | Paladin     |
| Blizzard          | `AHbz`  | Archmage    |
| Summon Water Elem.| `AHwe`  | Archmage    |
| Brilliance Aura   | `AHab`  | Archmage    |
| Mass Teleport     | `AHmt`  | Archmage    |
| Storm Bolt        | `AHtb`  | Mountain King |
| Thunder Clap      | `AHtc`  | Mountain King |
| Bash              | `AHbh`  | Mountain King |
| Avatar            | `AHav`  | Mountain King |
| Flame Strike      | `AHfs`  | Blood Mage  |
| Banish            | `AHbn`  | Blood Mage  |
| Siphon Mana       | `AHdr`  | Blood Mage  |
| Phoenix           | `AHpx`  | Blood Mage  |

### Orc

```bash
# Orc ground army
openwarcraft3 -data 'data/Warcraft III' +set sv_cheats 1 \
  +map 'Maps/(2)Rivercross.w3m' \
  +give unit ogru +give unit ogru +give unit orai +give unit otau

# Blademaster with Wind Walk and Critical Strike
openwarcraft3 -data 'data/Warcraft III' +set sv_cheats 1 \
  +map 'Maps/(2)Rivercross.w3m' \
  +give unit Obla AOwk AOcr

# Far Seer with Chain Lightning
openwarcraft3 -data 'data/Warcraft III' +set sv_cheats 1 \
  +map 'Maps/(2)Rivercross.w3m' \
  +give unit Ofar AOcl
```

Orc units:

| Unit              | Rawcode | Notes             |
|-------------------|---------|-------------------|
| Peon              | `opeo`  |                   |
| Grunt             | `ogru`  |                   |
| Raider            | `orai`  |                   |
| Tauren            | `otau`  |                   |
| Headhunter        | `ohun`  |                   |
| Shaman            | `oshm`  |                   |
| Witch Doctor      | `odoc`  |                   |
| Kodo Beast        | `okod`  |                   |
| Wind Rider        | `owyv`  |                   |
| Batrider          | `otbr`  | TFT               |
| Spirit Walker     | `ospw`  | TFT               |
| Blademaster       | `Obla`  | Hero              |
| Far Seer          | `Ofar`  | Hero              |
| Tauren Chieftain  | `Otch`  | Hero              |
| Shadow Hunter     | `Oshd`  | Hero, TFT         |

Orc hero abilities:

| Ability           | Rawcode | Hero             |
|-------------------|---------|------------------|
| Wind Walk         | `AOwk`  | Blademaster      |
| Mirror Image      | `AOmi`  | Blademaster      |
| Critical Strike   | `AOcr`  | Blademaster      |
| Bladestorm        | `AObd`  | Blademaster      |
| Chain Lightning   | `AOcl`  | Far Seer         |
| Far Sight         | `AOfs`  | Far Seer         |
| Feral Spirit      | `AOsf`  | Far Seer         |
| Earthquake        | `AOeq`  | Far Seer         |
| Shockwave         | `AOsh`  | Tauren Chieftain |
| War Stomp         | `AOws`  | Tauren Chieftain |
| Endurance Aura    | `AOae`  | Tauren Chieftain |
| Reincarnation     | `AOre`  | Tauren Chieftain |

### Undead

```bash
# Undead army
openwarcraft3 -data 'data/Warcraft III' +set sv_cheats 1 \
  +map 'Maps/(2)Rivercross.w3m' \
  +give unit ugho +give unit ugho +give unit uabo +give unit uban

# Death Knight with Death Coil and Unholy Aura
openwarcraft3 -data 'data/Warcraft III' +set sv_cheats 1 \
  +map 'Maps/(2)Rivercross.w3m' \
  +give unit Udea AUdc AUau

# Lich with Frost Nova and Dark Ritual
openwarcraft3 -data 'data/Warcraft III' +set sv_cheats 1 \
  +map 'Maps/(2)Rivercross.w3m' \
  +give unit Ulic AUfn AUdr
```

Undead units:

| Unit              | Rawcode | Notes             |
|-------------------|---------|-------------------|
| Acolyte           | `uaco`  |                   |
| Ghoul             | `ugho`  |                   |
| Crypt Fiend       | `ucry`  |                   |
| Gargoyle          | `ugar`  |                   |
| Abomination       | `uabo`  |                   |
| Banshee           | `uban`  |                   |
| Necromancer       | `unec`  |                   |
| Meat Wagon        | `umtw`  |                   |
| Frost Wyrm        | `ufro`  |                   |
| Obsidian Statue   | `uobs`  | TFT               |
| Destroyer         | `ubsp`  | TFT               |
| Death Knight      | `Udea`  | Hero              |
| Lich              | `Ulic`  | Hero              |
| Dreadlord         | `Udre`  | Hero              |
| Crypt Lord        | `Ucry`  | Hero, TFT         |

Undead hero abilities:

| Ability           | Rawcode | Hero         |
|-------------------|---------|--------------|
| Death Coil        | `AUdc`  | Death Knight |
| Death Pact        | `AUdp`  | Death Knight |
| Unholy Aura       | `AUau`  | Death Knight |
| Animate Dead      | `AUan`  | Death Knight |
| Frost Nova        | `AUfn`  | Lich         |
| Frost Armor       | `AUfa`  | Lich         |
| Dark Ritual       | `AUdr`  | Lich         |
| Death and Decay   | `AUdd`  | Lich         |
| Carrion Swarm     | `AUcs`  | Dreadlord    |
| Sleep             | `AUsl`  | Dreadlord    |
| Vampiric Aura     | `AUav`  | Dreadlord    |
| Inferno           | `AUin`  | Dreadlord    |

### Night Elf

```bash
# Night Elf army
openwarcraft3 -data 'data/Warcraft III' +set sv_cheats 1 \
  +map 'Maps/(2)Rivercross.w3m' \
  +give unit earc +give unit earc +give unit edot +give unit edob

# Keeper of the Grove with Entangling Roots and Force of Nature
openwarcraft3 -data 'data/Warcraft III' +set sv_cheats 1 \
  +map 'Maps/(2)Rivercross.w3m' \
  +give unit Ekee AEer AEfn

# Demon Hunter with Mana Burn and Evasion
openwarcraft3 -data 'data/Warcraft III' +set sv_cheats 1 \
  +map 'Maps/(2)Rivercross.w3m' \
  +give unit Edem AEmb AEev
```

Night Elf units:

| Unit              | Rawcode | Notes             |
|-------------------|---------|-------------------|
| Wisp              | `ewsp`  |                   |
| Archer            | `earc`  |                   |
| Huntress          | `esen`  |                   |
| Dryad             | `edry`  |                   |
| Druid of the Claw | `edoc`  |                   |
| Druid of the Talon| `edot`  |                   |
| Mountain Giant    | `emtg`  |                   |
| Hippogryph        | `ehip`  |                   |
| Chimera           | `echm`  |                   |
| Faerie Dragon     | `efdr`  | TFT               |
| Demon Hunter      | `Edem`  | Hero              |
| Keeper of Grove   | `Ekee`  | Hero              |
| Priestess of Moon | `Emoo`  | Hero              |
| Warden            | `Ewar`  | Hero, TFT         |

Night Elf hero abilities:

| Ability             | Rawcode | Hero              |
|---------------------|---------|-------------------|
| Mana Burn           | `AEmb`  | Demon Hunter      |
| Immolation          | `AEim`  | Demon Hunter      |
| Evasion             | `AEev`  | Demon Hunter      |
| Metamorphosis       | `AEme`  | Demon Hunter      |
| Entangling Roots    | `AEer`  | Keeper of Grove   |
| Force of Nature     | `AEfn`  | Keeper of Grove   |
| Thorns Aura         | `AEah`  | Keeper of Grove   |
| Tranquility         | `AEtq`  | Keeper of Grove   |
| Scout               | `AEst`  | Priestess of Moon |
| Searing Arrows      | `AHfa`  | Priestess of Moon |
| Trueshot Aura       | `AEar`  | Priestess of Moon |
| Starfall            | `AEsf`  | Priestess of Moon |
| Fan of Knives       | `AEfk`  | Warden            |
| Blink               | `AEbl`  | Warden            |
| Shadow Strike       | `AEsh`  | Warden            |
| Vengeance           | `AEsv`  | Warden            |

### Giving abilities to existing units

Select a unit first (click it in-game or use `hero select`), then:

```
give ability AHhb          # learn Holy Light on the selected unit
give ability AOwk          # learn Wind Walk on the selected unit
give ability AInv          # grant an inventory to a non-hero unit
```

### Spawning with abilities in one command

Trailing four-character arguments after the rawcode (and optional coordinates) are learned as abilities:

```
give unit Hpal AHhb AHds AHad           # Paladin with 3 abilities
give unit Hpal 128 128 AHhb AHds        # at position (128, 128) with 2 abilities
give unit Obla AOwk AOcr AOmi AObd      # Blademaster with full skill set
```

### WC3 headless screenshots

```bash
# Spawn human army and screenshot
openwarcraft3 -data 'data/Warcraft III' +set sv_cheats 1 \
  +map 'Maps/(2)Rivercross.w3m' \
  +give unit hfoo +give unit hkni +give unit Hpal AHhb \
  +vid_hidden 1 +screenshot 10 +com_frame_limit 20

# Use debugspawn for lower-level entity testing
openwarcraft3 -data 'data/Warcraft III' +set sv_cheats 1 \
  +map 'Maps/(2)Rivercross.w3m' \
  +debugspawn hfoo +vid_hidden 1 +screenshot 10 +com_frame_limit 20
```

## Combined workflows

### Verify a unit renders correctly

```bash
# 1. Spawn unit headless and capture
opensc2 -data data/StarCraft2 +set sv_cheats 1 +map Maps/TerranTest.SC2Components \
  +spawn SiegeTank +vid_hidden 1 +screenshot 5 +com_frame_limit 10

# 2. Check the output
ls screenshots/
open screenshots/shot0000.png
```

### Test god mode and kill

```bash
# SC2: spawn, toggle invulnerability, then kill
opensc2 -data data/StarCraft2 +set sv_cheats 1 +map Maps/TerranTest.SC2Components \
  +spawn Marine +god +kill

# WC3: spawn hero, grant abilities, toggle god, test kill
openwarcraft3 -data 'data/Warcraft III' +set sv_cheats 1 \
  +map 'Maps/(2)Rivercross.w3m' \
  +give unit Hpal AHhb +god
```

### Multi-race battle scene

```bash
# SC2: spawn both sides on the Terran test map
opensc2 -data data/StarCraft2 +set sv_cheats 1 +map Maps/TerranTest.SC2Components \
  +spawn Marine +spawn Marine +spawn SiegeTank \
  +spawn Zealot 2 60 55 +spawn Stalker 2 62 55

# WC3: spawn human vs orc on a melee map
openwarcraft3 -data 'data/Warcraft III' +set sv_cheats 1 \
  +map 'Maps/(2)Rivercross.w3m' \
  +give unit hfoo +give unit hkni +give unit Hpal AHhb \
  +give unit ogru 200 200 +give unit orai 210 200
```
