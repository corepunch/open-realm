/* Browser WASM32 does not support the existing 64-bit pointer-slot save format.
 * Do not compile g_save.c until its relocation encoding has a separate,
 * validated 32-bit representation. Never remove its pointer-size assertions.
 */
#include "games/warcraft-3/game/g_local.h"

bool WriteGame(cstring_t filename) {
    (void)filename;
    return false;
}

bool ReadGame(cstring_t filename) {
    (void)filename;
    return false;
}

bool G_GetSaveMap(cstring_t filename, string_t map, uint32_t map_size) {
    (void)filename;
    if (map && map_size) map[0] = '\0';
    return false;
}

void G_ClearSaveRegistries(void) {
}

bool G_SaveJassHandle(cstring_t type, handle_t value, uint32_t *id) {
    (void)type;
    (void)value;
    if (id) *id = UINT32_MAX;
    return false;
}

handle_t G_LoadJassHandle(cstring_t type, uint32_t id) {
    (void)type;
    (void)id;
    return NULL;
}
