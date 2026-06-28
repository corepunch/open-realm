# WoW Character Geoset Visibility Fix

## Goal

Fix forearm/hand and shin/foot transitions on WoW character models so arms fit
nicely into hands and legs into feet. The current renderer hardcodes which geoset
variant to show per body group, ignoring the outfit's `ItemDisplayInfo.dbc` geoset
data. This means glove/boot variants are never selected — the bare-hand and
bare-foot sections always display regardless of equipped items.

## Root Cause

`M2_CharacterGeosetVisible()` (`r_m2.c:2460`) had hardcoded visibility that
always showed the same section per group, ignoring the outfit's geoset data:

```c
// Old code:
case 8:  return section_id == 802;  // always variant 02, never consults outfit
case 9:  return section_id == 902;  // always variant 02, never consults outfit
```

The fix makes visibility data-driven using `outfit->geoset[group]` with correct
base offsets (800/900, not 802/902) so geoset=0 maps to nonexistent sections
that don't render, letting the base body show bare limbs.

## WoW Geoset System

Each M2 character model is divided into **geosets** — separate mesh parts
identified by `skin_section_id` in the `.skin` file. Section IDs are grouped by
hundreds:

| Group | Body Part | Section IDs | Base |
|-------|-----------|-------------|------|
| 4 | Hands/Gloves | 401-404 | 401 |
| 5 | Feet/Boots | 501-504 | 501 |
| 7 | Face | 702 | 702 |
| 8 | Lower arm/Wristbands | 802-803 | 800 |
| 9 | Lower leg/Kneepads | 902-903 | 900 |
| 13 | Upper leg | 1301 | 1301 |
| 15 | Base body | 1501 | 1501 |

**Critical**: Groups 8/9 base is 800/900, NOT 802/902. The model's first
overlay sections are 802/902, so geoset=0 (no item) must map to 800/900
(nonexistent section → no overlay shown → base body shows bare limb).

Each batch in the M2 has a `geoset_index` that tells which group it belongs to.
`ItemDisplayInfo.dbc` has three `geosetGroup` fields per item:

- `geosetGroup[0]`: gloves (group 4), boots (group 5), wristbands (group 8), or cape (group 15)
- `geosetGroup[1]`: kneepads (group 9)
- `geosetGroup[2]`: trousers (group 12) or robe (group 10/11)

The geoset value is a variant offset: `section_id = base + geoset_value`, where
base is defined per group. When geoset=0 and base=800/900, the nonexistent
section means no overlay renders — the base body mesh provides the bare limb.

## Implementation

### 1. Store `geoset_index` in `m2ModelBatch_t`

```c
typedef struct m2ModelBatch_s {
    ...
    WORD section_id;
    WORD geoset_index;   // NEW: M2 batch geoset group number
    ...
} m2ModelBatch_t;
```

Copied from `batch->geoset_index` during `M2_AddBatch()`.

### 2. Per-group geoset values in outfit

Replace the flat `geoset_group[3]` with a per-group array:

```c
#define M2_NUM_GEOSET_GROUPS 16

typedef struct {
    LPCSTR texture[M2_CHAR_TEX_COUNT];
    DWORD geoset[M2_NUM_GEOSET_GROUPS];  // geoset variant per group (0 = default)
    DWORD flags;
} m2CharacterOutfit_t;
```

### 3. Slot-to-group mapping

A data-driven table maps `(equipment_slot, geosetFieldIndex) → group_number`:

```c
static DWORD const slot_geoset_group_map[M2_SLOT_COUNT][3] = {
    /* GLOVES */ { 4, 0, 0 },
    /* BOOTS */  { 5, 0, 0 },
    /* CHEST */  { 8, 0, 12 },
    /* LEGS */   { 0, 9, 13 },
    /* CAPE */   { 15, 0, 0 },
};
```

When processing each item's `ItemDisplayInfo`, the geosetGroup values are stored
in `outfit->geoset[group]` using this mapping.

### 4. Equipment slot context

`M2_AddDisplayInfoToOutfit()` now takes a `slot` parameter. Callers:

- `M2_CharacterStartOutfit()`: maps CharStartOutfit.dbc field indices 14-25 to slots
  (head, shoulders, chest, shirt, belt, legs, boots, gloves, tabard, cape).
- `M2_ApplyEquipmentItems()`: passes the known slot for each equipment list
  (upper_body→CHEST, lower_body→LEGS, hand→GLOVES, foot→BOOTS).

### 5. Data-driven visibility

`M2_CharacterGeosetVisible()` reads `outfit->geoset[group]` and computes the
expected section_id:

```c
switch (group) {
    case 4:  expected = 401 + geoset; break;  // hands
    case 5:  expected = 501 + geoset; break;  // feet
    case 8:  expected = 800 + geoset; break;  // wristbands (base 800, not 802!)
    case 9:  expected = 900 + geoset; break;  // kneepads (base 900, not 902!)
    case 13: expected = 1301 + geoset; break; // upper leg
    ...
}
return section_id == expected;
```

When `geoset = 0` (no item), the nonexistent section is expected (800, 900),
so no overlay renders — the base body mesh provides the bare forearm/shin.
When `geoset >= 2`, the item-specific variant (802, 803, 902, 903) renders.

## Files Modified

- `games/world-of-warcraft/renderer/m2/r_m2.c`
  - `m2ModelBatch_t`: added `geoset_index` field
  - `M2_AddBatch()`: store `geoset_index`
  - `m2CharacterOutfit_t`: replaced `geoset_group[3]` with `geoset[16]`
  - Added `M2_NUM_GEOSET_GROUPS`, `M2_SLOT_*` enum, `slot_geoset_group_map[]`
  - `M2_AddDisplayInfoToOutfit()`: added `slot` parameter, uses mapping table
  - `M2_AddDisplayInfoListToOutfit()`: passes `slot`
  - `M2_AddEquipmentItemToOutfit()`: passes `slot`
  - `M2_ApplyEquipmentItems()`: passes slot for each call
  - `M2_CharacterStartOutfit()`: maps field indices to slots
  - `M2_CharacterGeosetVisible()`: data-driven visibility using `outfit->geoset[]`

## Behavior After Fix

- No item (`geoset = 0`): bare variants shown (401, 501) or no overlay (800, 900 nonexistent)
- Glove item with `geosetGroup[0] = 2` → hand shows section 403, forearm shows 802
- Boot item with `geosetGroup[0] = 2` → foot shows 503, shin shows 902
- Ruffled wristband (`geosetGroup[0] = 3` for chest) → forearm shows 803
- Bare forearm/shin from base body mesh when no group 8/9 overlays active
