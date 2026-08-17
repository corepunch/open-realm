#include "renderer/r_local.h"
#include "r_dbc.h"
#include "r_m2_utils.h"
#include <strings.h>

typedef struct {
    LPBYTE data;
    DWORD size, records, fields, record_size, string_size;
    BYTE const *records_base, *strings_base;
    int *index;
    DWORD index_capacity, index_field;
    BOOL tried, valid;
} M2DBC;

enum {
    M2_SLOT_NONE, M2_SLOT_HEAD, M2_SLOT_SHOULDERS, M2_SLOT_CHEST, M2_SLOT_SHIRT, M2_SLOT_BELT,
    M2_SLOT_LEGS, M2_SLOT_BOOTS, M2_SLOT_GLOVES, M2_SLOT_TABARD, M2_SLOT_CAPE, M2_SLOT_COUNT
};

typedef struct { DWORD display_ids[4]; } M2EQUIPMENTITEM;
typedef struct { DWORD race_id, gender_id; M2EQUIPMENTITEM items[256]; } M2EQUIPMENTSLOTITEMS;

static M2DBC char_start_outfit_dbc;
static M2DBC item_display_info_dbc;
static M2DBC char_sections_dbc;
static M2DBC creature_display_info_dbc;
static M2DBC creature_display_info_extra_dbc;
static m2CharSectionsLayout_t char_sections_layout;

/* DBCs stay as one resident file image; only the integer lookup table is runtime state. */
static BOOL M2_DbcLoad(M2DBC *dbc, LPCSTR filename) {
    int size;
    if (!dbc || !filename) return false;
    if (dbc->tried) return dbc->valid;
    dbc->tried = true;
    size = ri.FS_ReadFile(filename, (void **)&dbc->data);
    if (size <= 20 || !dbc->data || memcmp(dbc->data, "WDBC", 4)) {
        SAFE_DELETE(dbc->data, ri.FS_FreeFile);
        return false;
    }
    dbc->size = (DWORD)size;
    dbc->records = m2_read32(dbc->data + 4); dbc->fields = m2_read32(dbc->data + 8);
    dbc->record_size = m2_read32(dbc->data + 12); dbc->string_size = m2_read32(dbc->data + 16);
    if (!dbc->fields || dbc->record_size < sizeof(DWORD) ||
        20 + dbc->records * dbc->record_size + dbc->string_size > dbc->size) {
        SAFE_DELETE(dbc->data, ri.FS_FreeFile);
        memset(dbc, 0, sizeof(*dbc)); dbc->tried = true;
        return false;
    }
    dbc->records_base = dbc->data + 20;
    dbc->strings_base = dbc->records_base + dbc->records * dbc->record_size;
    dbc->valid = true;
    return true;
}

static void M2_DbcFree(M2DBC *dbc) {
    SAFE_DELETE(dbc->data, ri.FS_FreeFile);
    SAFE_DELETE(dbc->index, ri.MemFree);
    memset(dbc, 0, sizeof(*dbc));
}

static DWORD M2_DbcField(M2DBC const *dbc, BYTE const *record, DWORD field) {
    if (!dbc || !record || field >= dbc->fields || field * sizeof(DWORD) + sizeof(DWORD) > dbc->record_size)
        return 0;
    return m2_read32(record + field * sizeof(DWORD));
}

static LPCSTR M2_DbcString(M2DBC const *dbc, DWORD offset) {
    return dbc && dbc->valid && offset && offset < dbc->string_size ? (LPCSTR)(dbc->strings_base + offset) : NULL;
}

static DWORD M2_Fnv1a32(DWORD key) {
    DWORD hash = 2166136261u;
    FOR_LOOP(i, sizeof(key)) { hash ^= (key >> (i * 8)) & 0xffu; hash *= 16777619u; }
    return hash;
}

static BOOL M2_DbcBuildIndex(M2DBC *dbc, DWORD field) {
    DWORD capacity = 1;
    int *index;
    if (!dbc || !dbc->valid || field >= dbc->fields) return false;
    while (capacity < dbc->records * 2u) capacity <<= 1;
    index = ri.MemAlloc(capacity * sizeof(*index));
    if (!index) {
        fprintf(stderr, "M2 DBC: unable to allocate field %u index (%u entries)\n", field, dbc->records);
        return false;
    }
    FOR_LOOP(i, capacity) index[i] = -1;
    FOR_LOOP(i, dbc->records) {
        BYTE const *record = dbc->records_base + i * dbc->record_size;
        DWORD key = M2_DbcField(dbc, record, field);
        DWORD slot = M2_Fnv1a32(key) & (capacity - 1);
        while (index[slot] >= 0 && M2_DbcField(dbc, dbc->records_base + index[slot] * dbc->record_size,
                                               field) != key)
            slot = (slot + 1) & (capacity - 1);
        index[slot] = (int)i;
    }
    dbc->index = index; dbc->index_capacity = capacity; dbc->index_field = field;
    return true;
}

static BYTE const *M2_DbcFindKey(M2DBC *dbc, LPCSTR filename, DWORD field, DWORD key) {
    DWORD slot;
    if (!M2_DbcLoad(dbc, filename)) return NULL;
    if (!dbc->index && !M2_DbcBuildIndex(dbc, field)) return NULL;
    if (dbc->index_field != field) return NULL;
    slot = M2_Fnv1a32(key) & (dbc->index_capacity - 1);
    while (dbc->index[slot] >= 0) {
        BYTE const *record = dbc->records_base + dbc->index[slot] * dbc->record_size;
        if (M2_DbcField(dbc, record, field) == key) return record;
        slot = (slot + 1) & (dbc->index_capacity - 1);
    }
    return NULL;
}

static BYTE const *M2_DbcFindID(M2DBC *dbc, LPCSTR filename, DWORD id) {
    return M2_DbcFindKey(dbc, filename, 0, id);
}

BOOL M2_DbcCharacterRaceGender(LPCSTR model_path, LPDWORD race_id, LPDWORD gender_id) {
    LPCSTR character, race, gender;
    char race_name[64], gender_name[64];
    size_t length;
    if (!model_path || !race_id || !gender_id) return false;
    character = strcasestr(model_path, "Character\\");
    if (!character) character = strcasestr(model_path, "Character/");
    if (!character) return false;
    race = character + strlen("Character\\"); gender = strpbrk(race, "\\/");
    if (!gender || !(length = (size_t)(gender - race)) || length >= sizeof(race_name)) return false;
    memcpy(race_name, race, length); race_name[length] = '\0';
    gender++; length = strcspn(gender, "\\/.");
    if (!length || length >= sizeof(gender_name)) return false;
    memcpy(gender_name, gender, length); gender_name[length] = '\0';
    if (!strcasecmp(race_name, "Human")) *race_id = 1;
    else if (!strcasecmp(race_name, "Orc")) *race_id = 2;
    else if (!strcasecmp(race_name, "Dwarf")) *race_id = 3;
    else if (!strcasecmp(race_name, "NightElf")) *race_id = 4;
    else if (!strcasecmp(race_name, "Scourge") || !strcasecmp(race_name, "Undead")) *race_id = 5;
    else if (!strcasecmp(race_name, "Tauren")) *race_id = 6;
    else if (!strcasecmp(race_name, "Gnome")) *race_id = 7;
    else if (!strcasecmp(race_name, "Troll")) *race_id = 8;
    else if (!strcasecmp(race_name, "BloodElf")) *race_id = 10;
    else if (!strcasecmp(race_name, "Draenei")) *race_id = 11;
    else return false;
    if (!strcasecmp(gender_name, "Male")) *gender_id = 0;
    else if (!strcasecmp(gender_name, "Female")) *gender_id = 1;
    else return false;
    return true;
}

/* CreatureDisplayInfoExtra is decoded once into stable appearance and item display IDs. */
BOOL M2_DbcResolveCreatureAppearance(DWORD display_id, LPM2CREATUREAPPEARANCE out) {
    BYTE const *display, *extra;
    DWORD extra_id;
    if (!display_id || !out) return false;
    memset(out, 0, sizeof(*out));
    display = M2_DbcFindID(&creature_display_info_dbc, "DBFilesClient\\CreatureDisplayInfo.dbc", display_id);
    if (!display || creature_display_info_dbc.fields < 4) return false;
    extra_id = M2_DbcField(&creature_display_info_dbc, display, 3);
    extra = extra_id ? M2_DbcFindID(&creature_display_info_extra_dbc,
                                    "DBFilesClient\\CreatureDisplayInfoExtra.dbc", extra_id) : NULL;
    if (!extra || creature_display_info_extra_dbc.fields < 19) return false;
    out->appearance = Wow_PackAppearance((BYTE)M2_DbcField(&creature_display_info_extra_dbc, extra, 3), (BYTE)M2_DbcField(&creature_display_info_extra_dbc, extra, 4), (BYTE)M2_DbcField(&creature_display_info_extra_dbc, extra, 5), (BYTE)M2_DbcField(&creature_display_info_extra_dbc, extra, 6), (BYTE)M2_DbcField(&creature_display_info_extra_dbc, extra, 7), 1, 0);
    FOR_LOOP(i, sizeof(out->display_ids) / sizeof(out->display_ids[0])) out->display_ids[i] = 0;
    FOR_LOOP(i, MIN((DWORD)(sizeof(out->display_ids) / sizeof(out->display_ids[0])), creature_display_info_extra_dbc.fields - 8 - 1))
        out->display_ids[i] = M2_DbcField(&creature_display_info_extra_dbc, extra, 8 + i);
    return true;
}

/* Per wowdev DB:ItemDisplayInfo — for each slot, the three geosetGroup DBC fields
 * (indices 0/1/2 at fields 7/8/9) map to these geoset groups:
 *   LEGS: geosetGroup[0] → group 13 (trouser mesh), geosetGroup[1] → group 9 (kneepads) */
static DWORD const slot_geoset_group_map[M2_SLOT_COUNT][3] = {
    { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 8, 0, 13 }, { 8, 0, 0 }, { 0, 0, 0 },
    { 13, 9, 0 }, { 5, 0, 0 }, { 4, 0, 0 }, { 0, 0, 0 }, { 15, 0, 0 },
};

static void M2_DbcAddDisplayInfo(LPM2CHARACTEROUTFIT outfit, DWORD display_id, DWORD slot) {
    BYTE const *record;
    DWORD texture_base, geoset_base, flags_field;
    if (!outfit || !display_id || display_id == 0xffffffffu || slot == M2_SLOT_NONE || slot >= M2_SLOT_COUNT)
        return;
    record = M2_DbcFindID(&item_display_info_dbc, "DBFilesClient\\ItemDisplayInfo.dbc", display_id);
    if (!record) return;
    /* Classic's 23-field schema starts at 14; using the later offset shifted every starter clothing texture. */
    texture_base = m2_item_display_texture_base(item_display_info_dbc.fields);
    geoset_base = item_display_info_dbc.fields >= 22 ? 7 : 0;
    flags_field = item_display_info_dbc.fields >= 22 ? 10 : 0;
    FOR_LOOP(i, 3) {
        DWORD group = slot_geoset_group_map[slot][i];
        DWORD geoset = group ? M2_DbcField(&item_display_info_dbc, record, geoset_base + i) : 0;
        if (geoset) outfit->geoset[group] = geoset;
    }
    {
        DWORD flags = M2_DbcField(&item_display_info_dbc, record, flags_field);
        if (slot == M2_SLOT_HEAD && flags == 1) outfit->flags |= M2_CHAR_FLAG_HELM;
        outfit->flags |= flags;
    }
    FOR_LOOP(i, M2_CHAR_TEX_COMPONENT_COUNT) {
        LPCSTR texture = M2_DbcString(&item_display_info_dbc, M2_DbcField(&item_display_info_dbc, record, texture_base + i));
        signed char priority = Wow_CharacterTexturePriority(slot, i);
        if (texture && *texture && priority >= 0) outfit->texture[i][priority] = texture;
    }
    if (slot == M2_SLOT_CAPE)
        outfit->cape_texture = M2_DbcString(&item_display_info_dbc, M2_DbcField(&item_display_info_dbc, record, 3));
}

static M2EQUIPMENTITEM const *M2_DbcEquipmentItem(M2EQUIPMENTSLOTITEMS const *lists, DWORD count,
                                                  DWORD race_id, DWORD gender_id, BYTE item_index) {
    FOR_LOOP(i, count)
        if (lists[i].race_id == race_id && lists[i].gender_id == gender_id) return &lists[i].items[item_index];
    return NULL;
}

static void M2_DbcAddEquipmentItem(LPM2CHARACTEROUTFIT outfit, M2EQUIPMENTSLOTITEMS const *lists,
                                   DWORD count, DWORD race_id, DWORD gender_id, BYTE item_index, DWORD slot) {
    M2EQUIPMENTITEM const *item = M2_DbcEquipmentItem(lists, count, race_id, gender_id, item_index);
    if (!item) return;
    FOR_LOOP(i, 4) M2_DbcAddDisplayInfo(outfit, item->display_ids[i], slot);
}

static void M2_DbcApplyEquipment(LPM2CHARACTEROUTFIT outfit, DWORD race_id, DWORD gender_id, DWORD equipment) {
    static M2EQUIPMENTSLOTITEMS const upper[] = { { 2, 0, { [1] = { { 27274 } } } } };
    static M2EQUIPMENTSLOTITEMS const lower[] = { { 2, 0, { [1] = { { 27275 } } } } };
    static M2EQUIPMENTSLOTITEMS const hands[] = { { 2, 0, { [1] = { { 27271 } } } } };
    static M2EQUIPMENTSLOTITEMS const feet[] = { { 2, 0, { [1] = { { 27270 } } } } };
    wowEquipment_t items = Wow_UnpackEquipment(equipment);
    M2_DbcAddEquipmentItem(outfit, upper, 1, race_id, gender_id, items.upperBodyItem, M2_SLOT_CHEST);
    M2_DbcAddEquipmentItem(outfit, lower, 1, race_id, gender_id, items.lowerBodyItem, M2_SLOT_LEGS);
    M2_DbcAddEquipmentItem(outfit, hands, 1, race_id, gender_id, items.handItem, M2_SLOT_GLOVES);
    M2_DbcAddEquipmentItem(outfit, feet, 1, race_id, gender_id, items.footItem, M2_SLOT_BOOTS);
}

static BOOL M2_DbcStartOutfit(LPCSTR model_path, DWORD appearance, LPM2CHARACTEROUTFIT outfit) {
    DWORD race_id, gender_id, class_id, key;
    BYTE const *record;
    wowAppearance_t unpacked;
    if (!outfit || !M2_DbcCharacterRaceGender(model_path, &race_id, &gender_id)) return false;
    unpacked = Wow_UnpackAppearance(appearance); class_id = unpacked.classID ? unpacked.classID : 1;
    key = race_id | (class_id << 8) | (gender_id << 16);
    record = M2_DbcFindKey(&char_start_outfit_dbc, "DBFilesClient\\CharStartOutfit.dbc", 1, key);
    if (!record) return false;
    memset(outfit, 0, sizeof(*outfit));
    FOR_LOOP(i, 12) {
        DWORD display_id = M2_DbcField(&char_start_outfit_dbc, record, 14 + i);
        DWORD slot = Wow_CharacterSlotForInventoryType(M2_DbcField(&char_start_outfit_dbc, record, 26 + i));
        M2_DbcAddDisplayInfo(outfit, display_id, slot);
    }
    return true;
}

BOOL M2_DbcCharacterOutfit(LPCSTR model_path, DWORD appearance, DWORD equipment,
                           LPCM2CREATUREAPPEARANCE creature, LPM2CHARACTEROUTFIT outfit) {
    DWORD race_id, gender_id;
    if (!outfit || !M2_DbcCharacterRaceGender(model_path, &race_id, &gender_id)) return false;
    if (creature) {
        memset(outfit, 0, sizeof(*outfit));
        FOR_LOOP(i, sizeof(creature->display_ids) / sizeof(creature->display_ids[0]))
            M2_DbcAddDisplayInfo(outfit, creature->display_ids[i], Wow_CharacterCreatureItemSlot(i));
        return true;
    }
    if (!M2_DbcStartOutfit(model_path, appearance, outfit)) return false;
    M2_DbcApplyEquipment(outfit, race_id, gender_id, equipment);
    return true;
}

BOOL M2_DbcCharacterVariationTexturePath(LPCSTR model_path, DWORD section_index, DWORD variation_index,
                                         DWORD color_index, DWORD texture_index, LPSTR out, DWORD out_size) {
    DWORD race_id, gender_id, variation_field, color_field, texture_base;
    if (!out || !out_size || texture_index >= 3 ||
        !M2_DbcCharacterRaceGender(model_path, &race_id, &gender_id) ||
        !M2_DbcLoad(&char_sections_dbc, "DBFilesClient\\CharSections.dbc")) return false;
    if (!char_sections_layout) {
        char_sections_layout = char_sections_dbc.fields < 10 ? M2_CHAR_SECTIONS_INVALID : m2_char_sections_layout(char_sections_dbc.records_base, char_sections_dbc.records, char_sections_dbc.record_size);
        if (char_sections_layout == M2_CHAR_SECTIONS_INVALID)
            fprintf(stderr, "M2 DBC: unsupported CharSections schema (%u fields, %u-byte records)\n",
                    char_sections_dbc.fields, char_sections_dbc.record_size);
    }
    if (char_sections_layout == M2_CHAR_SECTIONS_INVALID) return false;
    /* Classic is variation-first; treating its color as Wrath's field 9 made nonzero skin colors fail lookup. */
    variation_field = char_sections_layout == M2_CHAR_SECTIONS_TEXTURE_FIRST ? 8 : 4;
    color_field = char_sections_layout == M2_CHAR_SECTIONS_TEXTURE_FIRST ? 9 : 5;
    texture_base = char_sections_layout == M2_CHAR_SECTIONS_TEXTURE_FIRST ? 4 : 6;
    FOR_LOOP(i, char_sections_dbc.records) {
        BYTE const *record = char_sections_dbc.records_base + i * char_sections_dbc.record_size;
        LPCSTR texture;
        if (M2_DbcField(&char_sections_dbc, record, 1) != race_id ||
            M2_DbcField(&char_sections_dbc, record, 2) != gender_id ||
            M2_DbcField(&char_sections_dbc, record, 3) != section_index ||
            M2_DbcField(&char_sections_dbc, record, variation_field) != variation_index ||
            M2_DbcField(&char_sections_dbc, record, color_field) != color_index) continue;
        texture = M2_DbcString(&char_sections_dbc, M2_DbcField(&char_sections_dbc, record, texture_base + texture_index));
        if (texture && *texture) { snprintf(out, out_size, "%s", texture); return true; }
        /* Classic male hair rows intentionally omit strings; the archive naming contract supplies the DBC-selected color. */
        if (section_index == 3 && gender_id == 0 && texture_index == 0) {
            PATHSTR derived;
            if (m2_classic_hair_texture_path(model_path, color_index, derived)) {
                snprintf(out, out_size, "%s", derived); return true;
            }
        }
    }
    return false;
}

BOOL M2_DbcCharacterTexturePathForType(LPCSTR model_path, DWORD appearance, DWORD texture_type,
                                      LPSTR out, DWORD out_size) {
    wowAppearance_t a = Wow_UnpackAppearance(appearance);
    switch (texture_type) {
        case 1: return M2_DbcCharacterVariationTexturePath(model_path, 0, 0, a.skinColorID, 0, out, out_size);
        case 2: return M2_DbcCharacterVariationTexturePath(model_path, 4, 0, a.skinColorID, 0, out, out_size);
        case 6:
            return M2_DbcCharacterVariationTexturePath(model_path, 3, a.hairStyleID, a.hairColorID, 0, out, out_size);
        case 8: return M2_DbcCharacterVariationTexturePath(model_path, 0, 0, a.skinColorID, 1, out, out_size);
        default: return false;
    }
}

void M2_DbcShutdown(void) {
    M2_DbcFree(&char_start_outfit_dbc); M2_DbcFree(&item_display_info_dbc);
    M2_DbcFree(&char_sections_dbc); M2_DbcFree(&creature_display_info_dbc);
    M2_DbcFree(&creature_display_info_extra_dbc);
    char_sections_layout = M2_CHAR_SECTIONS_UNKNOWN;
}
