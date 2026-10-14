#ifndef WC3_DIFFICULTY_H
#define WC3_DIFFICULTY_H

#include "common/shared.h"
#ifndef _WIN32
#include <strings.h>
#endif

#define WC3_CAMPAIGN_DIFFICULTY_CVAR "wc3_campaign_difficulty"
#define WC3_CAMPAIGN_DIFFICULTY_DEFAULT "1"

enum {
    WC3_CAMPAIGN_DIFFICULTY_EASY = 0,
    WC3_CAMPAIGN_DIFFICULTY_NORMAL = 1,
    WC3_CAMPAIGN_DIFFICULTY_HARD = 2,
};

/* The campaign selector has three choices; Insane (3) remains valid as a
 * gamedifficulty handle, but is not an archived campaign-menu preference. */
static inline bool WC3_ParseCampaignDifficulty(cstring_t text, uint32_t *out) {
    uint32_t difficulty = 0;
    if (!text || !*text || !out) return false;
    for (; *text; text++) {
        if (*text < '0' || *text > '9') return false;
        difficulty = difficulty * 10 + (uint32_t)(*text - '0');
        if (difficulty > WC3_CAMPAIGN_DIFFICULTY_HARD) return false;
    }
    *out = difficulty;
    return true;
}

static inline uint32_t WC3_CampaignDifficultyOrNormal(cstring_t text) {
    uint32_t difficulty;
    return WC3_ParseCampaignDifficulty(text, &difficulty)
        ? difficulty : WC3_CAMPAIGN_DIFFICULTY_NORMAL;
}

static inline bool WC3_IsCampaignMap(cstring_t path) {
    if (!path) return false;
    return !strncasecmp(path, "Maps\\Campaign\\", 14) ||
           !strncasecmp(path, "Maps/Campaign/", 14) ||
           !strncasecmp(path, "Maps\\FrozenThrone\\Campaign\\", 26) ||
           !strncasecmp(path, "Maps/FrozenThrone/Campaign/", 26);
}

/* Only fresh map creation uses this preference. Restart overrides, scripted
 * level transitions, and saved-game state belong to separate lifecycle paths. */
static inline uint32_t WC3_InitialGameDifficulty(cstring_t path, cstring_t preference) {
    return WC3_IsCampaignMap(path) ? WC3_CampaignDifficultyOrNormal(preference)
                                   : WC3_CAMPAIGN_DIFFICULTY_NORMAL;
}

#endif
