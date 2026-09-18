#include "g_local.h"

/* Routing consumes game-owned surface policy; only this edict contract contains WC3 destructable state. */
static BOOL entity_is_live_walkable_surface(edict_t const *ent) {
    return ent && ent->destructable.initialized && !ent->destructable.dead &&
        ent->destructable.placement_solid && ent->pathtex &&
        ent->data.DestructableData && ent->data.DestructableData->walkable;
}

static BYTE entity_dynamic_pathing_flags(edict_t const *ent) {
    return M_UnitStaticPathingFlags(ent);
}

/* WC3 pathing TGAs are transposed relative to model/world axes.  Only bridge
 * targets use the authored angle to select a quarter-turn; ordinary footprints
 * retain their existing unrotated contract. */
static void entity_pathtex_transform(pathTexTransformParams_t const *params, pathTexTransform_t *transform) {
    pathTex_t const *pt = params ? params->pathtex : NULL;
    FLOAT const angle = params && params->ent ? params->ent->s.angle : 0.0f;
    int quarter;

    if (!transform || !pt) return;
    quarter = (pt->width != pt->height) + (int)lroundf(angle / ((FLOAT)M_PI / 2.0f));
    transform->turn = ((quarter % 4) + 4) % 4;
    transform->width = transform->turn & 1 ? pt->height : pt->width;
    transform->height = transform->turn & 1 ? pt->width : pt->height;
    if (!params->ent || params->ent->targtype != TARG_BRIDGE)
        transform->turn = 0, transform->width = pt->width, transform->height = pt->height;
}

static inline HANDLE G_WorldReadFile(LPCSTR filename, LPDWORD size) { return gi.ReadFile(filename, size); }
static inline HANDLE G_WorldMemAlloc(long size) { return gi.MemAlloc(size); }
static inline void G_WorldMemFree(HANDLE mem) { gi.MemFree(mem); }
static inline void G_WorldSetPriorityArchive(HANDLE archive) { gi.SetPriorityArchive(archive); }
static inline BOMStatus G_WorldTextRemoveBom(LPSTR buffer) {
	size_t len;
	if (!buffer) return INVALID_BOM;
	len = strlen(buffer);
	if (len >= 3 && !memcmp(buffer, "\xEF\xBB\xBF", 3)) { memmove(buffer, buffer + 3, len - 2); return UTF8_BOM_FOUND; }
	if (len >= 2 && !memcmp(buffer, "\xFF\xFE", 2)) { memmove(buffer, buffer + 2, len - 1); return UTF16LE_BOM_FOUND; }
	if (len >= 2 && !memcmp(buffer, "\xFE\xFF", 2)) { memmove(buffer, buffer + 2, len - 1); return UTF16BE_BOM_FOUND; }
	return NO_BOM;
}
#define FS_ReadFile G_WorldReadFile
#define FS_FreeFile G_WorldMemFree
#define FS_SetPriorityArchive G_WorldSetPriorityArchive
#define MemAlloc G_WorldMemAlloc
#define MemFree G_WorldMemFree
#define PF_TextRemoveBom G_WorldTextRemoveBom
#define Com_Error(code, ...) gi.error(__VA_ARGS__)
/* ELF otherwise binds server world calls to the executable's client copy, leaving routing state uninitialized. */
#pragma GCC visibility push(hidden)
#include "common/world.c"
#include "common/world_w3.c"
#include "server/sv_routing.c"
#pragma GCC visibility pop

#undef FS_ReadFile
#undef FS_FreeFile
#undef FS_SetPriorityArchive
#undef MemAlloc
#undef MemFree
#undef PF_TextRemoveBom
#undef Com_Error
#undef ge
