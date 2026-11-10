#ifndef SC2_PRESENTATION_H
#define SC2_PRESENTATION_H

#include "sc2_combat.h"

#define SC2_COMMAND_SLOTS 15 // native CommandPanel is three rows of five

typedef struct {
    char face[64], type[32], abilcmd[128], submenu[64];
    uint32_t row, column, index;
    bool removed;
} sc2CardButton_t;

typedef struct {
    char id[64], parent[64], icon[256], name[128], tooltip[128], hotkey[128];
} sc2ButtonFace_t;

typedef struct {
    char name[128], icon[256], wireframe[256], portrait[256], portrait_image[256];
    float armor;
    sc2CardButton_t cards[SC2_COMMAND_SLOTS];
} sc2UnitPresentation_t;

typedef struct {
    char id[64], parent[64], effect[64], icon[256];
    float range, period, damage, splash;
    sc2DamageEffect_t effects[SC2_DAMAGE_EFFECTS];
    uint8_t effect_count;
} sc2WeaponPresentation_t;

bool SC2_MapUnitPresentation(cstring_t type, sc2UnitPresentation_t *out);
bool SC2_MapButtonFace(cstring_t face, sc2ButtonFace_t *out);
bool SC2_MapWeaponPresentation(cstring_t link, sc2WeaponPresentation_t *out);
#endif
