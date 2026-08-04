#include "s_skills.h"

/* Blight (Abli): Undead blight placement. Minimal implementation — sets
 * terrain blight at the target location. If the terrain renderer does not
 * support blight overlays yet, this is a TODO stub. */

static BOOL blight_selectlocation(LPEDICT clent, LPCVECTOR2 point) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    if (!caster || !point) {
        return false;
    }
    /* TODO: implement terrain blight overlay at point.
     * For now, just consume the command without effect. */
    return true;
}

static void blight_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
    S_SpellCursorSplat(clent, 200.0f);
    clent->client->menu.on_location_selected = blight_selectlocation;
}

ability_t a_blight = {
    .cmd = blight_command,
};
