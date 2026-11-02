#include "g_local.h"

/* Hash links contain only the latest version of each rawcode. Replaced records
 * remain allocated so an outer constructor survives nested metadata changes. */
static unitRuntimeType_t *unit_types[1024];
static unitRuntimeType_t *unit_types_allocated, *unit_type_last;
#ifdef BZ_TESTS
static uint32_t unit_type_resolutions;
uint32_t G_TestUnitTypeResolutions(bool reset) {
    uint32_t count = unit_type_resolutions;
    if (reset) unit_type_resolutions = 0;
    return count;
}
#endif

unitRuntimeType_t *G_UnitRuntimeType(uint32_t rawcode) {
    uint32_t version = G_UnitDataGeneration(), abilities = G_AbilityDataGeneration();
    unitRuntimeType_t *type = unit_type_last;
    if (type && type->rawcode == rawcode && type->version == version && type->ability_version == abilities)
        return type;
    uint32_t hash = rawcode ^ (rawcode >> 16);
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    unitRuntimeType_t **link = unit_types + (hash & 1023);
    while (*link && (*link)->rawcode != rawcode) link = &(*link)->hash_next;
    type = *link;
    if (type && type->version == version && type->ability_version == abilities)
        return unit_type_last = type;

    unitRuntimeType_t *prepared = gi.MemAlloc(sizeof(*prepared));
    memset(prepared, 0, sizeof(*prepared));
    prepared->rawcode = rawcode;
    prepared->version = version;
    prepared->ability_version = abilities;
    prepared->data = (edictData_s){
        .UnitProfile = G_UnitProfile(rawcode), .UnitBalance = G_UnitBalance(rawcode),
        .UnitData = G_UnitData(rawcode), .UnitUI = G_UnitUI(rawcode),
        .UnitWeapons = G_UnitWeapons(rawcode), .UnitAbilities = G_UnitAbil(rawcode),
        .Doodads = G_Doodad(rawcode), .ItemData = G_ItemData(rawcode),
        .DestructableData = G_DestructableData(rawcode)
    };
    prepared->hash_next = type ? type->hash_next : NULL;
    prepared->allocated_next = unit_types_allocated;
    unit_types_allocated = prepared;
    *link = prepared;
#ifdef BZ_TESTS
    unit_type_resolutions++;
#endif
    return unit_type_last = prepared;
}

void G_ResetUnitTypeBindings(void) {
    for (unitRuntimeType_t *type = unit_types_allocated; type; type = type->allocated_next)
        memset(&type->bindings, 0, sizeof(type->bindings));
}

void G_ClearUnitRuntimeTypes(void) {
    while (unit_types_allocated) {
        unitRuntimeType_t *type = unit_types_allocated;
        unit_types_allocated = type->allocated_next;
        gi.MemFree(type);
    }
    memset(unit_types, 0, sizeof(unit_types));
    unit_type_last = NULL;
}
