#ifndef G_WOW_CRAFTING_H
#define G_WOW_CRAFTING_H

#include <stdbool.h>
#include <stdint.h>

struct edict_s;
typedef struct {
    uint32_t spell, reagent, reagent_count, result, result_count;
    char const *name;
    char const *icon;
} wowRecipe_t;

uint32_t Wow_RecipeCount(void);
wowRecipe_t const *Wow_Recipe(uint32_t index);
bool Wow_Craft(struct edict_s *player, uint32_t spell);

#endif
