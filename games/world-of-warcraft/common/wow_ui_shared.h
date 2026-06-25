#ifndef wow_ui_shared_h
#define wow_ui_shared_h

#define WOW_UI_INVENTORY_SLOTS 6
#define WOW_UI_ACTION_SLOTS 12

/* Single userinfo-style cvar used to pass selected character data from UI to
   game module.  Format: \race\Human\sex\Male\class\1\appearance\12345
   The UI sets this before map load; the game module reads it in Wow_Init
   and stores the value in one CS_GENERAL configstring. */
#define WOW_CVAR_PLAYERINFO "wow_playerinfo"

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
