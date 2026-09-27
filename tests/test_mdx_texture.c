#include "test.h"
#include "games/warcraft-3/renderer/mdx/r_mdx.h"

TEST(mdx_texture, held_icon_replaces_only_slot_21) {
    mdxTexture_t texture = {0};
    mdxModel_t model = { .textures = &texture, .num_textures = 1 };
    texture_t icon = {0}, team = {0}, glow = {0};
    tr.texture[TEX_TEAM_COLOR] = &team;
    tr.texture[TEX_TEAM_GLOW] = &glow;
    T_ASSERT(MDLX_GetTexture(&model, 0, 0, 21, &icon, 21) == &icon);
    T_NULL(MDLX_GetTexture(&model, 0, 0, 31, &icon, 21));
    T_NULL(MDLX_GetTexture(&model, 0, 0, 0, &icon, 21));
    T_ASSERT(MDLX_GetTexture(&model, 0, 0, 1, &icon, 21) == &team);
    T_ASSERT(MDLX_GetTexture(&model, 0, 0, 2, &icon, 21) == &glow);
    T_ASSERT(MDLX_GetTexture(&model, 0, 0, 31, &icon, 0) == &icon);
    tr.texture[TEX_TEAM_COLOR] = tr.texture[TEX_TEAM_GLOW] = NULL;
}
