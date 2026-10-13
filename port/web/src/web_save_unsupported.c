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
