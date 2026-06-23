#ifndef wow_ui_shared_h
#define wow_ui_shared_h

#define WOW_UI_INVENTORY_SLOTS 6
#define WOW_UI_ACTION_SLOTS 12

/* Cvar names used to pass selected character data from UI to game module.
   The UI sets these before map load; the game module reads them in Wow_Init
   and stores the values in CS_GENERAL configstrings. */
#define WOW_CVAR_RACE      "wow_race"
#define WOW_CVAR_SEX       "wow_sex"
#define WOW_CVAR_CLASS     "wow_class"
#define WOW_CVAR_APPEARANCE "wow_appearance"

typedef enum {
    WOW_STAT_HEALTH = 0,
    WOW_STAT_HEALTH_MAX = 1,
    WOW_STAT_POWER = 2,
    WOW_STAT_POWER_MAX = 3,
    WOW_STAT_LEVEL = 4,
    WOW_STAT_XP = 5,
    WOW_STAT_XP_MAX = 6,
    WOW_STAT_COPPER = 7,
} wowPlayerStat_t;

#endif
