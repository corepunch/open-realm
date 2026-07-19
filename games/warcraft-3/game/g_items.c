#include "g_local.h"

static FLOAT G_MiscVectorValue(LPCSTR name, DWORD index) {
    LPCSTR value = FS_FindSheetCell(game.config.misc, "Misc", name);
    if (!value) {
        return 0;
    }

    for (DWORD i = 0; i < index; i++) {
        value = strchr(value, ',');
        if (!value) {
            return 0;
        }
        value++;
    }

    return atof(value);
}

void SP_SpawnItem(LPEDICT self) {
    PATHSTR model_filename;
    strlcpy(model_filename, ITEM_FILE(self->class_id), sizeof(model_filename));
    self->s.model = G_RegisterModel(model_filename);
    self->s.shadow = G_LoadShadowTexture(FS_FindSheetCell(game.config.misc, "Misc", "ItemShadowFile"), false);
    self->s.shadow_rect = ShadowPackRect(
        G_MiscVectorValue("ItemShadowOffset", 0),
        G_MiscVectorValue("ItemShadowOffset", 1),
        G_MiscVectorValue("ItemShadowSize", 0),
        G_MiscVectorValue("ItemShadowSize", 1));
    self->movetype = MOVETYPE_NONE;
}
