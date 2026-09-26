#ifndef WOW_R_DBC_H
#define WOW_R_DBC_H

#include "common/shared.h"

#define M2_NUM_GEOSET_GROUPS 16
#define M2_CHAR_TEX_PRIORITIES 7
#define M2_CHAR_FLAG_KNEELENGTH 0x4u

/* HelmetGeosetVisData race-resolved hide bits, indexed by geoset group/100
 * (group 0 = head/hair, 1 = beard, 2 = sideburns, 3 = moustache, 7 = ears). */
#define M2_HELM_HIDE_HAIR      0x01u   /* group 0 */
#define M2_HELM_HIDE_BEARD     0x02u   /* group 1 */
#define M2_HELM_HIDE_SIDEBURNS 0x04u   /* group 2 */
#define M2_HELM_HIDE_MOUSTACHE 0x08u   /* group 3 */
#define M2_HELM_HIDE_EARS      0x80u   /* group 7 */

enum {
    M2_CHAR_TEX_UPPER_ARM,
    M2_CHAR_TEX_LOWER_ARM,
    M2_CHAR_TEX_HAND,
    M2_CHAR_TEX_UPPER_TORSO,
    M2_CHAR_TEX_LOWER_TORSO,
    M2_CHAR_TEX_UPPER_LEG,
    M2_CHAR_TEX_LOWER_LEG,
    M2_CHAR_TEX_FOOT,
    M2_CHAR_TEX_COMPONENT_COUNT
};

typedef struct {
    cstring_t texture[M2_CHAR_TEX_COMPONENT_COUNT][M2_CHAR_TEX_PRIORITIES];
    cstring_t cape_texture;
    cstring_t helm_model;          /* ItemDisplayInfo model name stem (head slot) */
    cstring_t shoulder_model[2];   /* left / right shoulder model name stems */
    cstring_t helm_texture;        /* ItemDisplayInfo model texture stem (head slot) */
    cstring_t shoulder_texture[2]; /* left / right shoulder model texture stems */
    uint32_t helm_vis_id[2];       /* HelmetGeosetVisData ids (male, female) from ItemDisplayInfo */
    uint32_t helm_hide;            /* race-resolved geoset hide mask (M2_HELM_HIDE_*) */
    uint32_t geoset[M2_NUM_GEOSET_GROUPS];
    uint32_t flags;
} m2CharacterOutfit_t;



typedef struct {
    uint32_t appearance;
    uint32_t display_ids[11];
} m2CreatureAppearance_t;



bool M2_DbcResolveCreatureAppearance(uint32_t display_id, m2CreatureAppearance_t *out);
bool M2_DbcCharacterOutfit(cstring_t model_path, uint32_t appearance, uint32_t equipment, m2CreatureAppearance_t const *creature, m2CharacterOutfit_t *outfit);
bool M2_DbcCharacterRaceGender(cstring_t model_path, uint32_t *race_id, uint32_t *gender_id);
bool M2_DbcCharacterVariationTexturePath(cstring_t model_path, uint32_t section_index, uint32_t variation_index, uint32_t color_index, uint32_t texture_index, string_t out, uint32_t out_size);
bool M2_DbcCharacterTexturePathForType(cstring_t model_path, uint32_t appearance, uint32_t texture_type, string_t out, uint32_t out_size);
void M2_DbcShutdown(void);

#endif
