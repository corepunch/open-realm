# StarCraft II — Unit Sound System

## Sound Catalog Files

SC2 sounds are defined in XML game data inside `.SC2Mod`/`.SC2Data` archives:

| File | Purpose |
|------|---------|
| `GameData/SoundData.xml` | `CSound` asset definitions — file paths, volume, category, 3D settings |
| `GameData/ActorData.xml` | `CActorSound` + `CActorUnit` — maps game events to sound actors |

## CSound Definition (SoundData.xml)

Each `<CSound id="...">` entry describes one sound asset:

```xml
<CSound id="Marine_Stimpack" parent="SoundOneShot">
    <On Terms="Abil.Stimpack.SourceCastStart" Send="Create"/>
</CSound>
```

Key attributes/child elements:

| Element | Description |
|---------|-------------|
| `<AssetArray File="..."/>` | WAV/OGG file path(s); `##id##` substitution supported |
| `<Category value="Voice\|Combat\|Music\|..."/>` | Audio bus routing |
| `<Mode value="3DWorld\|2D"/>` | Spatialization mode |
| `<Volume value="-6,-6"/>` | Min/max volume in dB |
| `<VolumeRolloffPoints/>` | Distance attenuation curve |
| `<Select value="Shuffle\|Sequential"/>` | File selection from array |
| `<ReverbBalance Room="..."/>` | EAX reverb mix |

## CActorSound / CActorUnit Wiring (ActorData.xml)

Unit sounds are wired through the Actor system using event terms:

```xml
<CActorUnit id="GenericUnitBase" ...>
    <EventDataSound Actor="UnitSound"/>
    <SoundArray index="Birth"  value="##unitName##_Birth"/>
    <SoundArray index="Ready"  value="##unitName##_Ready"/>
    <SoundArray index="What"   value="##unitName##_What"/>
    <SoundArray index="Yes"    value="##unitName##_Yes"/>
    <SoundArray index="Attack" value="##unitName##_Attack"/>
    <DeathActorSound value="UnitDeathSound"/>
    <DeathActorVoice value="UnitDeathVoice"/>
</CActorUnit>
```

`##unitName##` is substituted with the unit's data ID (e.g. `Marine`). Sound naming convention: `Marine_What`, `Marine_Attack`, `Marine_Birth`, etc.

## Marine Example

```xml
<CActorUnit id="Marine" parent="GenericUnitBase" unitName="Marine">
    <DeathArray index="Normal" ModelLink="MarineDeath"
                SoundLink="Marine_DeathFXBloodSpray"
                VoiceLink="MarineDeathVoice"/>
    <On Terms="WeaponStart.*.AttackStart" Send="AnimBracketStart Attack Attack"/>
    <On Terms="WeaponStop.*.AttackStop"  Send="AnimBracketStop Attack"/>
</CActorUnit>
```

Death sound is specified per death type (`Normal`, `Blast`, `Disintegrate`, `Fire`, etc.) via `SoundLink` and `VoiceLink`.

## Sound Event Flow

1. Game engine fires a unit event (e.g. `WeaponStart.*.AttackStart`).
2. Actor system matches it against `<On Terms="...">` rules.
3. Matched rule sends a message (`Create`, `AnimBracketStart`, etc.) to the linked sound actor.
4. `CActorSound` creates/plays the associated `CSound` asset.

## Key Differences from WC3

| Aspect | WC3 | SC2 |
|--------|-----|-----|
| Sound catalog | SLK tables (flat key→files) | XML `CSound` with inheritance |
| Wiring | Label string + naming convention | Actor event terms system |
| Random files | Comma-separated in SLK | `<AssetArray>` with `Select="Shuffle"` |
| Death variation | Single death per unit | Multiple by death type (`Normal`, `Blast`, `Fire`, …) |
| 3D attenuation | Min/Max/Cutoff distance fields | `VolumeRolloffPoints` curve |
