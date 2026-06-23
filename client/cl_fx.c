#include "client.h"
#include "sound/s_local.h"
#include "common/shared.h"

/* Fire one-shot sound for an entity that carries a non-zero event.
 * Called from CL_ReadPacketEntities after each entity delta is applied.
 * The server sets ent->event and ent->sound simultaneously for the event
 * frame; g_main.c resets both to zero at the start of each RunFrame so
 * the one-shot fires exactly once. */
void CL_EntityEvent(entityState_t const *ent) {
    if (!ent->event || !ent->sound || ent->sound >= MAX_SOUNDS) return;
    LPCSTR path = cl.configstrings[CS_SOUNDS + ent->sound];
    if (path && path[0])
        S_PlaySoundFile(path);
}


