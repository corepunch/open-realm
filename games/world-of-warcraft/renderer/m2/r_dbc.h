#ifndef WOW_R_DBC_H
#define WOW_R_DBC_H

#include "common/shared.h"

#define M2_NUM_GEOSET_GROUPS 16
#define M2_CHAR_TEX_PRIORITIES 7
#define M2_CHAR_FLAG_KNEELENGTH 0x4u

/* HelmetGeosetVisData race-resolved hide bits, indexed by geoset group/100. */
#define M2_HELM_HIDE_HAIR     0x02u   /* group 100 */
#define M2_HELM_HIDE_BEARD    0x04u   /* group 200 */
#define M2_HELM_HIDE_EARRINGS 0x08u   /* group 300 */
#define M2_HELM_HIDE_EARS     0x80u   /* group 700 */

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
    LPCSTR texture[M2_CHAR_TEX_COMPONENT_COUNT][M2_CHAR_TEX_PRIORITIES];
    LPCSTR cape_texture;
    LPCSTR helm_model;          /* ItemDisplayInfo model name stem (head slot) */
    LPCSTR shoulder_model[2];   /* left / right shoulder model name stems */
    DWORD helm_vis_id[2];       /* HelmetGeosetVisData ids (male, female) from ItemDisplayInfo */
    DWORD helm_hide;            /* race-resolved geoset hide mask (M2_HELM_HIDE_*) */
    DWORD geoset[M2_NUM_GEOSET_GROUPS];
    DWORD flags;
} M2CHARACTEROUTFIT;
typedef M2CHARACTEROUTFIT *LPM2CHARACTEROUTFIT;
typedef M2CHARACTEROUTFIT const *LPCM2CHARACTEROUTFIT;

typedef struct {
    DWORD appearance;
    DWORD display_ids[11];
} M2CREATUREAPPEARANCE;
typedef M2CREATUREAPPEARANCE *LPM2CREATUREAPPEARANCE;
typedef M2CREATUREAPPEARANCE const *LPCM2CREATUREAPPEARANCE;

BOOL M2_DbcResolveCreatureAppearance(DWORD display_id, LPM2CREATUREAPPEARANCE out);
BOOL M2_DbcCharacterOutfit(LPCSTR model_path, DWORD appearance, DWORD equipment, LPCM2CREATUREAPPEARANCE creature, LPM2CHARACTEROUTFIT outfit);
BOOL M2_DbcCharacterRaceGender(LPCSTR model_path, LPDWORD race_id, LPDWORD gender_id);
BOOL M2_DbcCharacterVariationTexturePath(LPCSTR model_path, DWORD section_index, DWORD variation_index, DWORD color_index, DWORD texture_index, LPSTR out, DWORD out_size);
BOOL M2_DbcCharacterTexturePathForType(LPCSTR model_path, DWORD appearance, DWORD texture_type, LPSTR out, DWORD out_size);
void M2_DbcShutdown(void);

#endif
