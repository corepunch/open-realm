#include "g_local.h"

void SP_SpawnItem(LPEDICT self) {
    PATHSTR model_filename;
    strcpy(model_filename, ITEM_FILE(self->class_id));
    self->s.model = G_RegisterModel(model_filename);
    self->movetype = MOVETYPE_NONE;
}
