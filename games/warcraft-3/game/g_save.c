#include "g_local.h"

static DWORD const save_magic = MAKEFOURCC('W', '3', 'S', 'V');
static DWORD const save_version = 3;

/* Every pointer in edict_s that survives a save must be represented here. */
field_t fields[] = {
    EDICTFIELD(class_id, F_INT),
    EDICTFIELD(variation, F_INT),
    EDICTFIELD(build_project, F_INT),
    EDICTFIELD(spawn_time, F_INT),
    EDICTFIELD(harvested_lumber, F_INT),
    EDICTFIELD(harvested_gold, F_INT),
    EDICTFIELD(heatmap2, F_INT),
    EDICTFIELD(peonsinside, F_INT),
    EDICTFIELD(aiflags, F_INT),
    EDICTFIELD(damage, F_INT),
    EDICTFIELD(collision, F_FLOAT),
    EDICTFIELD(s.origin, F_VECTOR),
    EDICTFIELD(construction.primary_builder, F_EDICT),
    EDICTFIELD(rally.entity, F_EDICT),
    EDICTFIELD(revival.producer, F_EDICT),
    EDICTFIELD(revival.queue_next, F_EDICT),
    EDICTFIELD(goldmine.mine, F_EDICT),
    EDICTFIELD(inventory, F_EDICT, MAX_INVENTORY),
    EDICTFIELD(cargo.units, F_EDICT, MAX_CARGO),
    EDICTFIELD(item.carrier, F_EDICT),
    EDICTFIELD(ground_next, F_EDICT),
    EDICTFIELD(movement.attackmove_waypoint, F_EDICT),
    EDICTFIELD(movement.patrol_a, F_EDICT),
    EDICTFIELD(movement.patrol_b, F_EDICT),
    EDICTFIELD(movement.patrol_target, F_EDICT),
    EDICTFIELD(goalentity, F_EDICT),
    EDICTFIELD(combatentity, F_EDICT),
    EDICTFIELD(secondarygoal, F_EDICT),
    EDICTFIELD(owner, F_EDICT),
    EDICTFIELD(build, F_EDICT),
    { NULL, 0, 0, 0 }
};

static BOOL SaveBytes(FILE *f, LPCVOID data, size_t size) { return fwrite(data, 1, size, f) == size; }
static BOOL LoadBytes(FILE *f, void *data, size_t size) { return fread(data, 1, size, f) == size; }

/* Convert entity and client pointers to stable save-file indexes. */
static size_t field_size(fieldtype_t type) {
    switch (type) {
    case F_INT: return sizeof(int);
    case F_FLOAT: return sizeof(float);
    case F_VECTOR: return sizeof(VECTOR3);
    case F_EDICT: return sizeof(LPEDICT);
    case F_CLIENT: return sizeof(LPGAMECLIENT);
    default: return 0;
    }
}

static void WriteField1(field_t const *field, BYTE *base) {
    size_t size = field_size(field->type);
    DWORD count = field->array_size ? field->array_size : 1;
    int index;

    if (!size) return;
    FOR_LOOP(i, count) {
        void *p = base + field->ofs + i * size;
        switch (field->type) {
        case F_EDICT: index = *(LPEDICT *)p ? (int)(*(LPEDICT *)p - g_edicts) : -1; *(int *)p = index; break;
        case F_CLIENT: index = *(LPGAMECLIENT *)p ? (int)(*(LPGAMECLIENT *)p - game.clients) : -1; *(int *)p = index; break;
        default: break;
        }
    }
}

/* Restore entity and client pointers after the raw edict block is read. */
static BOOL ReadField(field_t const *field, BYTE *base) {
    size_t size = field_size(field->type);
    DWORD count = field->array_size ? field->array_size : 1;

    if (!size) return true;
    FOR_LOOP(i, count) {
        void *p = base + field->ofs + i * size;
        int index = *(int *)p;
        switch (field->type) {
        case F_EDICT:
            if (index < -1 || index >= globals.max_edicts) return false;
            *(LPEDICT *)p = index < 0 ? NULL : g_edicts + index;
            break;
        case F_CLIENT:
            if (index < -1 || index >= game.max_clients) return false;
            *(LPGAMECLIENT *)p = index < 0 ? NULL : game.clients + index;
            break;
        default: break;
        }
    }
    return true;
}

static BOOL WriteEdict(FILE *f, LPCEDICT ent) {
    edict_t temp = *ent;
    field_t const *field;

    /* Process-owned pointers are rebound from class data after loading. */
    temp.client = NULL; temp.pathtex = NULL; temp.area.prev = NULL; temp.area.next = NULL;
    temp.destructable.alive_pathtex = NULL; temp.destructable.death_pathtex = NULL;
    temp.destructable.drop_sets = NULL; temp.destructable.drop_sets_count = 0;
    temp.added_abilities = NULL; temp.added_abilities_count = 0;
    temp.removed_abilities = NULL; temp.removed_abilities_count = 0;
    temp.permanent_abilities = NULL; temp.permanent_abilities_count = 0;
    temp.animation = NULL; temp.currentmove = NULL;
    temp.stand = NULL; temp.birth = NULL; temp.prethink = NULL; temp.think = NULL;
    temp.die = NULL; temp.idle = NULL; temp.move = NULL; temp.run = NULL; temp.attack = NULL; temp.pain = NULL;
    temp.UnitProfile = NULL; temp.UnitBalance = NULL; temp.UnitData = NULL; temp.UnitUI = NULL;
    temp.UnitWeapons = NULL; temp.UnitAbilities = NULL; temp.Doodads = NULL;
    temp.ItemData = NULL; temp.DestructableData = NULL;
    for (field = fields; field->name; field++) WriteField1(field, (BYTE *)&temp);
    return SaveBytes(f, &temp, sizeof(temp));
}

static BOOL WriteClient(FILE *f, LPCGAMECLIENT client) {
    GAMECLIENT temp = *client;

    /* Client pointers and callbacks are process-owned; text storage remains inline in GAMECLIENT. */
    temp.ps.name = NULL;
    memset(temp.ps.texts, 0, sizeof(temp.ps.texts));
    temp.mapplayer = NULL;
    temp.menu.on_entity_selected = NULL; temp.menu.on_location_selected = NULL;
    temp.menu.cmdbutton = NULL; temp.menu.refresh = NULL;
    temp.camera.target_controller = NULL;
    return SaveBytes(f, &temp, sizeof(temp));
}

static BOOL ReadClient(FILE *f, LPGAMECLIENT client) {
    if (!LoadBytes(f, client, sizeof(*client))) return false;
    FOR_LOOP(i, PLAYERTEXT_COUNT) client->ps.texts[i] = client->playerTextCursor[i] ?
        client->playerTextStorage[i][client->playerTextCursor[i] & PLAYER_TEXT_MASK] : NULL;
    client->mapplayer = NULL;
    client->menu.on_entity_selected = NULL; client->menu.on_location_selected = NULL;
    client->menu.cmdbutton = NULL; client->menu.refresh = NULL;
    client->camera.target_controller = NULL;
    return true;
}

static BOOL ReadEdict(FILE *f, LPEDICT ent) {
    field_t const *field;

    if (!LoadBytes(f, ent, sizeof(*ent))) return false;
    for (field = fields; field->name; field++) if (!ReadField(field, (BYTE *)ent)) return false;
    if (ent->class_id) G_BindEntityData(ent);
    return true;
}

BOOL WriteGame(LPCSTR filename) {
    FILE *f = fopen(filename, "wb");
    struct { DWORD magic, version, edict_size, num_edicts, max_clients; } header = {
        save_magic, save_version, sizeof(edict_t), globals.num_edicts, game.max_clients
    };

    if (!f) return false;
    if (!SaveBytes(f, &header, sizeof(header)) || !SaveBytes(f, &level.framenum, sizeof(level.framenum)) ||
        !SaveBytes(f, &level.time, sizeof(level.time)) || !SaveBytes(f, &level.started, sizeof(level.started)) ||
        !SaveBytes(f, &level.scriptsStarted, sizeof(level.scriptsStarted))) { fclose(f); return false; }
    FOR_LOOP(i, game.max_clients) if (!WriteClient(f, game.clients + i)) { fclose(f); return false; }
    FOR_LOOP(i, globals.num_edicts) {
        BOOL used = g_edicts[i].inuse;
        if (!SaveBytes(f, &used, sizeof(used)) || (used && !SaveBytes(f, &i, sizeof(i)))) { fclose(f); return false; }
        if (used && !WriteEdict(f, g_edicts + i)) { fclose(f); return false; }
    }
    fclose(f);
    return true;
}

BOOL ReadGame(LPCSTR filename) {
    FILE *f = fopen(filename, "rb");
    struct { DWORD magic, version, edict_size, num_edicts, max_clients; } header;
    DWORD index;

    if (!f) return false;
    if (!LoadBytes(f, &header, sizeof(header)) || header.magic != save_magic || header.version != save_version ||
        header.edict_size != sizeof(edict_t) || header.num_edicts > globals.max_edicts ||
        header.max_clients != game.max_clients) { fclose(f); return false; }
    if (!LoadBytes(f, &level.framenum, sizeof(level.framenum)) || !LoadBytes(f, &level.time, sizeof(level.time)) ||
        !LoadBytes(f, &level.started, sizeof(level.started)) || !LoadBytes(f, &level.scriptsStarted, sizeof(level.scriptsStarted))) {
        fclose(f); return false;
    }
    FOR_LOOP(i, game.max_clients) if (!ReadClient(f, game.clients + i)) { fclose(f); return false; }
    memset(g_edicts, 0, sizeof(edict_t) * globals.max_edicts);
    globals.num_edicts = header.num_edicts;
    FOR_LOOP(i, header.num_edicts) {
        BOOL used;
        if (!LoadBytes(f, &used, sizeof(used))) { fclose(f); return false; }
        if (!used) continue;
        if (!LoadBytes(f, &index, sizeof(index)) || index >= globals.max_edicts || !ReadEdict(f, g_edicts + index)) {
            fclose(f); return false;
        }
    }
    FOR_LOOP(i, game.max_clients) g_edicts[i].client = game.clients + i;
    FOR_LOOP(i, globals.num_edicts) if (g_edicts[i].inuse && gi.LinkEntity) gi.LinkEntity(g_edicts + i);
    fclose(f);
    return true;
}
