#ifndef wow_ui_shared_h
#define wow_ui_shared_h

#define WOW_UI_INVENTORY_SLOTS 16 // slots; 6 start-items + 10 looted; sent via svc_unit_ui
#define WOW_UI_ACTION_SLOTS 12
#define WOW_UI_MAX_MESSAGES 8
#define WOW_UI_MESSAGE_TITLE 128
#define WOW_UI_MESSAGE_BODY 512

typedef struct {
    DWORD message_id;
    BYTE kind;
    BYTE flags;
    DWORD quest_id;
    char title[WOW_UI_MESSAGE_TITLE];
    char body[WOW_UI_MESSAGE_BODY];
} wowUiMessage_t;

#define WOW_UI_MESSAGE_UNREAD 1
#define WOW_UI_MESSAGE_QUEST_REWARD 1

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
    WOW_STAT_CAST_PROGRESS = 8,   /* ms remaining on current cast (0 = idle) */
    WOW_STAT_CAST_MAX = 9,        /* total cast duration in ms; client computes ratio */
    WOW_STAT_SELECTED_ACTION = 10, /* currently highlighted action bar slot (0-11, or 255=none) */
} wowPlayerStat_t;

#endif
