# Objects

XML file listing all entities placed on the map by the editor.

Source: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/Objects

## Purpose

Contains placed `Unit`, `Doodad`, `Point`, and `Camera` records. This is the primary source for
entity positions, rotations, player assignments, and unit type IDs. Unit types are catalog keys —
they must be resolved through `UnitData.xml` → `ActorData.xml` → `ModelData.xml` to reach an
`.m3` file path.

## Root Element

```xml
<PlacedObjects version="26">
    ...
</PlacedObjects>
```

Two known casings observed (beta vs release maps):
- `version` (lowercase) — normal release maps
- `Version` (uppercase) — beta phase 2 maps

## `<Unit>`

Places a unit (including resources, start locations, and anything from the unit catalog).

```xml
<Unit
    id="?"
    variation="?"
    position="x,y,z"
    rotation="?"
    scale="x,y,z"
    section="?"
    unitType="CatalogID"
    player="0"
    resources="0">
    <AIFlag  index="IsUsable" value="1"/>
    <AIRebuild index="?" value="1"/>
    <Flag    index="?" value="?"/>
</Unit>
```

| Attribute | Type | Notes |
| --- | --- | --- |
| `id` | string | Unique object ID within the file |
| `variation` | int | Model variation index |
| `position` | vec3 | World position `x,y,z`; z is height above terrain |
| `rotation` | float | Yaw angle in radians (or degrees — verify per map) |
| `scale` | vec3 | Per-axis scale |
| `section` | int | Editor section grouping; ignored at runtime |
| `unitType` | string | **Catalog ID** — key into `UnitData.xml`; not an `.m3` path |
| `player` | int | 0-based player index |
| `resources` | int | Starting resources for resource units |

Child elements:
- `<AIFlag index="..." value="...">` — AI flag; e.g. `index="IsUsable" value="1"`
- `<AIRebuild index="..." value="...">` — Whether unit is rebuildable after death
- `<Flag index="..." value="...">` — Generic unit flag

## `<Doodad>`

Places a decorative doodad (prop, terrain feature, destructible).

```xml
<Doodad
    id="?"
    variation="?"
    position="x,y,z"
    rotation="?"
    scale="x,y,z"
    section="?"
    type="CatalogID"
    tintColor="r,g,b,a">
    <Flag index="?" value="?"/>
</Doodad>
```

| Attribute | Type | Notes |
| --- | --- | --- |
| `id` | string | Unique object ID |
| `variation` | int | Model variation index |
| `position` | vec3 | World position |
| `rotation` | float | Yaw angle |
| `scale` | vec3 | Per-axis scale |
| `section` | int | Editor section grouping |
| `type` | string | Catalog ID for the doodad type |
| `tintColor` | vec4 | RGBA tint, 0–255 per channel |

## `<Point>`

A named map point, optionally carrying a model, sound, and pathing radii.

```xml
<Point
    id="?"
    variation="?"
    position="x,y,z"
    rotation="?"
    scale="x,y,z"
    section="?"
    type="?"
    name="StartLocation"
    color="?"
    model="Assets\..."
    animProps="?"
    sound="?"
    attachID="?"
    objectID="?"
    objectType="?"
    PathingSoftRadius="?"
    PathingHardRadius="?"/>
```

| Attribute | Notes |
| --- | --- |
| `type` | Point type string (e.g. `StartLocation`) |
| `name` | Human-readable point name |
| `model` | Optional direct `.m3` path — one of the few places where a model path appears directly |
| `animProps` | Animation properties string |
| `sound` | Sound asset path |
| `PathingSoftRadius` | Soft pathing exclusion radius |
| `PathingHardRadius` | Hard pathing exclusion radius |

## `<Camera>`

A named camera placement with target and optional camera-value overrides.

```xml
<Camera
    id="?"
    variation="?"
    position="x,y,z"
    rotation="?"
    scale="x,y,z"
    section="?"
    name="?"
    color="?"
    target="x,y,z">
    <CameraValue index="Distance" value="34"/>
    <CameraValue index="Pitch"    value="56"/>
    <Flag index="?" value="?"/>
</Camera>
```

| Attribute | Notes |
| --- | --- |
| `name` | Camera name referenced in triggers |
| `target` | Look-at position `x,y,z` |

`<CameraValue>` children carry named camera parameters:

| `index` | Meaning |
| --- | --- |
| `Distance` | Distance from target |
| `Pitch` | Vertical angle in degrees |
| `Yaw` | Horizontal angle in degrees |
| `FOV` | Field of view in degrees |
| `NearClip` | Near clip plane distance |
| `FarClip` | Far clip plane distance |
| `HeightOffset` | Camera height offset from terrain |

These correspond to the `camera_*` fields in `sc2Map_t` (parsed in `sc2_map.c`).

## Unit-To-Model Resolution

`unitType` in `<Unit>` is a catalog ID, not a model path. The resolution chain is:

```
unitType (CatalogID)
  → CUnit entry in UnitData.xml
      → actor reference (CActorUnit)
          → Model field
              → CModel entry in ModelData.xml
                  → Art: Model (.m3 path)
                  → Optional/Required .m3a animation files
```

Do not guess `Assets\Units\<Race>\<ID>\<ID>.m3` except as a last-resort fallback for tiny test fixtures. The actual path may differ from the unit ID due to shared models, variations, and campaign-specific actors.

## Parser Notes

- Parse `position` as three comma-separated floats. Accept both `"x,y,z"` and separate `x`/`y`/`z` attributes for compatibility with different map versions.
- Resources, start locations, and mineral patches are all `<Unit>` elements — distinguish them after catalog resolution, not by position or special-casing `unitType` strings.
- `<Doodad>` elements without models are common; do not require a model path to be present.
- The `section` attribute is editor metadata only; ignore it at runtime.
- The engine currently parses `Objects` in `sc2_map.c` and stores results in `sc2Map_t.objects[]` as `sc2MapObject_t` records.
