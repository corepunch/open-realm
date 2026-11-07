#include "client.h"
#include "menu_text_input.h"
#include "ui_layout.h"
#include "games/warcraft-3/common/player_colors.h"
#include <ctype.h>
#include <stdlib.h>
#include <SDL2/SDL.h>

bool scr_initialized;
static void SCR_GamepadDrawOverlay(void);

#define SCR_FPS_HEIGHT 8
#define SCR_FPS_BOTTOM_MARGIN 4
#define SCR_ALERT_PULSE_HALF_MS 250 // milliseconds; triangle-wave half period for transient command-button alert tint
#define SCR_ALERT_PULSE_MIN_GB 80 // color channel value; preserves portrait detail at the red peak of a transient alert

float SCR_UICanvasWidth(void) { return CL_Canvas()->scene.w; }

/*
 * SDL mouse positions are window pixels, while UI/layout coordinates use the
 * engine's virtual canvas.  Cursor drawing and FDF hit-testing must share this
 * mapping, including each game's authored widescreen scene.  The canvas keeps
 * the window it was resolved from, so a frame never mixes an old scene with a
 * new window size.
 */
vec2_t SCR_ScreenToUI(int x, int y) {
    uiCanvas_t const *canvas = CL_Canvas();
    float nx = 0.0f, ny = 0.0f;

    if (canvas->window.width > 0 && canvas->window.height > 0) {
        nx = (float)x / (float)canvas->window.width;
        ny = (float)y / (float)canvas->window.height;
    }

    return MAKE(vec2_t, nx * canvas->scene.w, ny * canvas->scene.h);
}

static void SCR_DrawString(int x, int y, cstring_t string) {
    if (string) re.DrawString(x, y, string);
}

static void SCR_DrawFPS(uint32_t msec) {
    static uint32_t elapsed = 0;
    static uint32_t frames_drawn = 0;
    static uint32_t fps = 0;
    char text[64];
    size2_t window = re.GetWindowSize();
    uint32_t inset = SCR_FPS_HEIGHT + SCR_FPS_BOTTOM_MARGIN;
    uint32_t y = window.height > inset ? window.height - inset : 0;

    elapsed += msec;
    frames_drawn++;
    if (elapsed >= 500) {
        fps = frames_drawn * 1000 / elapsed;
        elapsed = 0;
        frames_drawn = 0;
    } else if (!fps && msec > 0) {
        fps = 1000 / msec;
    }

    if (fps) {
        snprintf(text, sizeof(text), "FPS %u  Drawcalls %u", (unsigned)fps, (unsigned)re.GetDrawCalls());
    } else {
        snprintf(text, sizeof(text), "FPS --  Drawcalls %u", (unsigned)re.GetDrawCalls());
    }
    SCR_DrawString(10, y, text);
}

/*
 * Preserve the SDL cursor selected by the input module while the game renderer
 * temporarily replaces it with authored cursor content.
 */
static void SCR_UpdateSystemCursor(bool game_cursor_active) {
    static int previous_visibility = -2;

    if (game_cursor_active) {
        if (previous_visibility == -2) {
            previous_visibility = SDL_ShowCursor(SDL_QUERY);
            if (previous_visibility < 0) previous_visibility = SDL_ENABLE;
            SDL_ShowCursor(SDL_DISABLE);
        }
        return;
    }

    if (previous_visibility != -2) {
        SDL_ShowCursor(previous_visibility ? SDL_ENABLE : SDL_DISABLE);
        previous_visibility = -2;
    }
}

static color32_t SCR_CursorTint(void) {
    uint32_t const entnum = cl.hover_entity;

    if (entnum && entnum < MAX_CLIENT_ENTITIES) {
        entityState_t const *state = &cl.ents[entnum].current;
        if (CL_EntityAllowsWorldHover(state)) {
            if (state->flags & EF_HOSTILE) {
                return MAKE(color32_t, 255, 0, 0, 255);
            }
            if ((state->flags & EF_NEUTRAL) || state->player != cl.playerstate.number) {
                return MAKE(color32_t, 255, 255, 0, 255);
            }
            return MAKE(color32_t, 0, 255, 0, 255);
        }
    }
    return COLOR32_WHITE;
}

static void SCR_DrawCursor(void) {
    int const x = (int)mouse.origin.x, y = (int)mouse.origin.y;
    bool drawn = false;

    /* Loading plaques are non-interactive. Hide both the authored cursor and
     * the native SDL cursor until normal menu/game presentation resumes. */
    if (cl.playerstate.client_ui_state == CLIENT_UI_LOADING) {
        SCR_UpdateSystemCursor(true);
        return;
    }

    if (Cvar_Integer("r_cursor", 0) == 1) {
        drawCursor_t cursor = { .origin = SCR_ScreenToUI(x, y), .scroll = CL_MouseScroll(), .tint = SCR_CursorTint(), .time = CL_RealTime() };
        cursor.hover = cl.hover_entity && cl.hover_entity < MAX_CLIENT_ENTITIES &&
            CL_EntityAllowsWorldHover(&cl.ents[cl.hover_entity].current);
        cursor.hostile = cursor.hover && (cl.ents[cl.hover_entity].current.flags & EF_HOSTILE);
        cursor.owned = cursor.hover && cl.ents[cl.hover_entity].current.player == cl.playerstate.number;
        cursor.game = cls.state == ca_active;
        uint16_t const model = cl.playerstate.stats[UI_PLAYERSTAT_CURSOR_INTERACTIONL];
        uint16_t const image = cl.playerstate.stats[UI_PLAYERSTAT_CURSOR_IMAGE];
        cursor.model = cursor.game && model && model < MAX_MODELS ? cl.models[model] : NULL;
        cursor.skin = cursor.game && image && image < MAX_IMAGES ? cl.pics[image] : NULL;
        cursor.interaction = cl.playerstate.stats[UI_PLAYERSTAT_CURSOR_INTERACTION];
        if (!CL_GameplayInputReady() || CL_MouseCaptured()) {
            cursor.interaction = 0;
            cursor.hover = false;
            cursor.skin = NULL;
        }
        drawn = re.DrawCursor(&cursor);
    }
    SCR_UpdateSystemCursor(drawn);
}

void SCR_BeginLoadingPlaque(void) {
    if (cls.disable_screen)
        return;
    if (cls.state == ca_disconnected)
        return;
    if (cls.key_dest == key_console)
        return;
    SCR_UpdateScreen(0);
    cls.disable_screen = SDL_GetTicks();
    cls.disable_servercount = -1;
}

void SCR_EndLoadingPlaque(void) {
    Sys_UpdateLoadingPresentation(false, 1.0f, true);
    cls.disable_screen = 0;
}

/* Server-authored screen overlay: the textured filter wins; cinefade is the
 * untextured black overlay for games that only publish a fade alpha. */
void SCR_SetupViewBlend(viewDef_t *view, player_t const *ps, texture_t const *const *pics) {
    uint32_t const image = ps->cinefilter_image;

    view->blendTexture = NULL;
    view->blendColor = MAKE(color32_t, 0, 0, 0, 0);
    view->blendMode = BLEND_MODE_BLEND;
    view->blendUV = MAKE(rect_t, 0, 0, 1, 1);
    view->blendWrap = TEXMAP_FLAG_NONE;
    if (ps->cinefilter_color.a > 0) {
        box2_t const uv = ps->cinefilter_uv;
        view->blendTexture = image > 0 && image < MAX_IMAGES ? pics[image] : NULL;
        view->blendColor = ps->cinefilter_color;
        view->blendMode = (BLEND_MODE)ps->cinefilter_blendmode;
        view->blendUV = MAKE(rect_t, uv.min.x, uv.min.y, uv.max.x - uv.min.x, uv.max.y - uv.min.y);
        view->blendWrap = (TEXMAP_FLAGS)(ps->cinefilter_texmapflags & TEXMAP_FLAG_WRAP_UV);
    } else if (ps->cinefade > 0) {
        view->blendColor.a = (uint8_t)(MIN(ps->cinefade, 1.0f) * 255);
    }
}

void SCR_DrawScreenField(uint32_t msec) {
    SCR_GamepadBeginUIFrame();
    re.BeginFrame();
    if (CL_MovieActive()) {
        SCR_UpdateSystemCursor(true);
        CL_MovieDraw();
#ifndef BZ_TESTS
        if (CL_ScreenshotReady()) re.Screenshot();
#endif
        re.EndFrame();
        return;
    }
    switch (cls.state) {
    default:
        Com_Error(ERR_FATAL, "SCR_DrawScreenField: bad cls.state");
        break;
    case ca_disconnected:
        if (CL_MenuActive()) menu.Refresh(cl.time);
        break;
    case ca_connecting:
    case ca_connected:
        if (cl.playerstate.client_ui_state == CLIENT_UI_LOADING) SCR_DrawLoadingLayout();
        else if (CL_MenuActive()) menu.Refresh(cl.time);
        break;
    case ca_active:
        if (CL_WindowPresentationActive()) {
            /* Presentation replaces the world view instead of drawing over
             * the last terrain frame. Keep mandatory navigation visible even
             * when the user has disabled the normal HUD with r_hud 0. */
            size2_t const size = re.GetWindowSize();
            rect_t const screen = { 0, 0, size.width, size.height };
            re.DrawFill(&screen, MAKE(color32_t, 0, 0, 0, 255));
            CL_WindowDraw();
        } else {
            V_RenderView();
            if (Cvar_Integer("r_hud", 1)) SCR_DrawLayout();
        }
        break;
    }

    CON_DrawConsole();
    if (Cvar_Integer("scr_showfps", 0)) {
        SCR_DrawFPS(msec);
    }

    SCR_GamepadDrawOverlay();

    /* Cursor is deliberately last so it stays above world, HUD, menus and debug text. */
    SCR_DrawCursor();
#ifndef BZ_TESTS
    if (CL_ScreenshotReady()) {
        re.Screenshot();
    }
#endif
    re.EndFrame();
}

void SCR_UpdateLoadingPlaque(void) {
    if (!scr_initialized) return;
    if (!cls.disable_screen) return;
    if (Cvar_Integer("r_norefresh", 0)) return;
    if (cl.playerstate.client_ui_state != CLIENT_UI_LOADING) return;
    SCR_DrawScreenField(0);
    Sys_UpdateLoadingPresentation(true, cl.loading_progress, cl.layout[LAYER_LOADING] != NULL);
}

void SCR_UpdateScreen(uint32_t msec) {
    static int recursive;
    static uint32_t no_refresh_elapsed, no_refresh_frames;

    if (!scr_initialized) {
        return;
    }

    /* Quake-style no-refresh mode preserves input, snapshots, and server work while submitting no screen frame. */
    if (Cvar_Integer("r_norefresh", 0)) {
        if (Cvar_Integer("r_stats", 0)) {
            no_refresh_elapsed += msec; no_refresh_frames++;
            if (no_refresh_elapsed >= 1000) {
                fprintf(stderr, "[R_NOREFRESH] loops=%u\n", (unsigned)((uint64_t)no_refresh_frames * 1000 / no_refresh_elapsed));
                no_refresh_elapsed = 0; no_refresh_frames = 0;
            }
        } else no_refresh_elapsed = no_refresh_frames = 0;
        return;
    }
    no_refresh_elapsed = 0; no_refresh_frames = 0;

    if (cls.disable_screen) {
        if (SDL_GetTicks() - cls.disable_screen > 120000) {
            cls.disable_screen = 0;
            fprintf(stderr, "Loading plaque timed out.\n");
        }
        return;
    }

    if (++recursive > 2) {
        Com_Error(ERR_FATAL, "SCR_UpdateScreen: recursively called");
    }
    recursive = 1;

    SCR_DrawScreenField(msec);

    recursive = 0;
}

/* --------------------------------------------------------------------------
 * Layout system — server-authored UI frame rendering and hit testing.
 * Previously in cl_unit_layout.c; merged here because the "unit_" prefix
 * was misleading — this is general-purpose layout, not unit-specific.
 * -------------------------------------------------------------------------- */

#define MAX_LISTBOX_TEXT 2048

static void SCR_GamepadDrawOverlay(void);
static cstring_t active_tooltip = NULL;

#define GAMEPAD_RADIAL_SLOTS 10
#define GAMEPAD_RADIAL_PAGES 6
#define GAMEPAD_NAV_MAX 512
#define GAMEPAD_QUEUE_MAX 16
#define GAMEPAD_NAV_MIN_STEP 0.005f // virtual-canvas units; ignores pixel-rounding jitter at the focused button
static bool layout_current_window;
static void SCR_GamepadCaptureFrame(uiFrame_t const *frame);
/* Presentation caches are rebuilt from the game's real clickable frames each
 * draw. The client does not invent abilities, prices, or enabled state. */
typedef struct {
    uiFrame_t frame;
    uiFrame_t autocast_sparkle;
    char command[CMDARG_LEN * 2];
    uiCommandButton_t command_state;
    float authored_icon_px;
    uint32_t sparkle_layer;
    bool active_glow, present, sparkle_present;
} gamepadRadialItem_t;
/* Keep the exact WC3 tooltip presenter (backdrop, font, wrapping) associated
 * with the command bar so radial focus can render the same authored tooltip. */
typedef struct {
    uiFrame_t frame;
    uiTooltip_t payload;
    rect_t rect;
    bool present;
} gamepadRadialTooltip_t;
static gamepadRadialTooltip_t gamepad_radial_tooltip;
static gamepadRadialItem_t gamepad_radial[GAMEPAD_RADIAL_PAGES][GAMEPAD_RADIAL_SLOTS];
static struct { vec2_t center; bool modal; } gamepad_nav[GAMEPAD_NAV_MAX];
static uint32_t gamepad_nav_count;
static struct {
    vec2_t center;
    uint32_t entity, row, column, columns;
    bool focused;
} gamepad_status_cells[MAX_SELECTED_ENTITIES];
static uint32_t gamepad_status_cell_count, gamepad_status_entity;
static struct { vec2_t center; uint32_t row; } gamepad_hero_cells[MAX_LAYOUT_OBJECTS];
static uint32_t gamepad_hero_cell_count, gamepad_hero_index;
static vec2_t gamepad_queue_cells[GAMEPAD_QUEUE_MAX];
static uint32_t gamepad_queue_cell_count, gamepad_queue_index;
void SCR_SetGamepadBuildQueueTargets(vec2_t const *targets, uint32_t count) {
    gamepad_queue_cell_count = MIN(count, GAMEPAD_QUEUE_MAX);
    if (targets && gamepad_queue_cell_count)
        memcpy(gamepad_queue_cells, targets, gamepad_queue_cell_count * sizeof(*targets));
}
static bool gamepad_panel_cursor_hero;
static struct {
    uint32_t clickable, command_buttons, missing_art, unsupported, invalid_position;
    uint32_t duplicate, captured;
    uint32_t last_report_ms;
} gamepad_radial_diag;
static bool gamepad_radial_open, gamepad_selection_visible;
static int gamepad_radial_page, gamepad_radial_sector = -1;
static vec2_t gamepad_selection_center;
static float gamepad_selection_radius;
static char local_notification[128];
static uint32_t local_notification_end_ms;

static font_t const *SCR_LocalNotificationFont(void) {
    font_t const *fallback = NULL;
    FOR_LOOP(i, MAX_FONTSTYLES) {
        font_t const *font = cl.fonts[i];
        cstring_t spec = cl.configstrings[CS_FONTS + i];
        cstring_t size = spec ? strchr(spec, ',') : NULL;
        if (!font) continue;
        if (!fallback) fallback = font;
        if (size && !strcmp(size + 1, "10")) return font;
    }
    return fallback;
}

void SCR_ShowLocalNotification(cstring_t text) {
    if (!text || !*text) {
        local_notification[0] = '\0';
        local_notification_end_ms = 0;
        return;
    }
    strlcpy(local_notification, text, sizeof(local_notification));
    local_notification_end_ms = SDL_GetTicks() + 2000;
}

void SCR_GamepadBeginUIFrame(void) {
    gamepad_nav_count = 0;
    gamepad_status_cell_count = 0;
    gamepad_hero_cell_count = 0;
    gamepad_queue_cell_count = 0;
    memset(gamepad_radial, 0, sizeof(gamepad_radial));
    memset(&gamepad_radial_tooltip, 0, sizeof(gamepad_radial_tooltip));
    memset(&gamepad_radial_diag, 0, offsetof(typeof(gamepad_radial_diag), last_report_ms));
}

static bool SCR_GamepadStatusCellScreenPosition(uint32_t index, vec2_t *position) {
    uiCanvas_t const *canvas = CL_Canvas();
    if (!position || index >= gamepad_status_cell_count || canvas->scene.w <= 0.0f ||
        canvas->scene.h <= 0.0f || canvas->window.width <= 0.0f || canvas->window.height <= 0.0f)
        return false;
    position->x = gamepad_status_cells[index].center.x * canvas->window.width / canvas->scene.w;
    position->y = gamepad_status_cells[index].center.y * canvas->window.height / canvas->scene.h;
    return true;
}

static bool SCR_GamepadHeroCellScreenPosition(uint32_t index, vec2_t *position) {
    uiCanvas_t const *canvas = CL_Canvas();
    if (!position || index >= gamepad_hero_cell_count || canvas->scene.w <= 0.0f ||
        canvas->scene.h <= 0.0f || canvas->window.width <= 0.0f || canvas->window.height <= 0.0f)
        return false;
    position->x = gamepad_hero_cells[index].center.x * canvas->window.width / canvas->scene.w;
    position->y = gamepad_hero_cells[index].center.y * canvas->window.height / canvas->scene.h;
    return true;
}

bool SCR_GamepadStatusPanelCursor(vec2_t *position) {
    if (!gamepad_status_cell_count) return false;
    uint32_t index = 0;
    FOR_LOOP(i, gamepad_status_cell_count) {
        if (gamepad_status_cells[i].focused) { index = i; break; }
    }
    gamepad_status_entity = gamepad_status_cells[index].entity;
    gamepad_panel_cursor_hero = false;
    return SCR_GamepadStatusCellScreenPosition(index, position);
}

bool SCR_GamepadBuildQueueCursor(vec2_t *position) {
    if (!position || !gamepad_queue_cell_count) return false;
    gamepad_queue_index = 0;
    uiCanvas_t const *canvas = CL_Canvas();
    if (canvas->scene.w <= 0.0f || canvas->scene.h <= 0.0f ||
        canvas->window.width <= 0.0f || canvas->window.height <= 0.0f) return false;
    position->x = gamepad_queue_cells[0].x * canvas->window.width / canvas->scene.w;
    position->y = gamepad_queue_cells[0].y * canvas->window.height / canvas->scene.h;
    return true;
}

bool SCR_GamepadBuildQueueMove(int direction, vec2_t *position) {
    uiCanvas_t const *canvas = CL_Canvas();
    if (!position || !gamepad_queue_cell_count || direction < 0 || direction > 3 ||
        canvas->scene.w <= 0.0f || canvas->scene.h <= 0.0f ||
        canvas->window.width <= 0.0f || canvas->window.height <= 0.0f) return false;
    float best = 1e30f;
    uint32_t next = gamepad_queue_index;
    FOR_LOOP(i, gamepad_queue_cell_count) {
        if (i == gamepad_queue_index) continue;
        float const dx = gamepad_queue_cells[i].x - gamepad_queue_cells[gamepad_queue_index].x;
        float const dy = gamepad_queue_cells[i].y - gamepad_queue_cells[gamepad_queue_index].y;
        float const parallel = direction == 0 ? -dy : direction == 1 ? dx :
                               direction == 2 ? dy : -dx;
        if (parallel <= 0.001f) continue;
        float const orthogonal = (direction & 1) ? dy : dx;
        /* Prefer targets on the same visual row/column, but allow crossing
         * between the raised active slot and the waiting row. */
        float const score = parallel + 2.5f * fabsf(orthogonal);
        if (score < best) { best = score; next = i; }
    }
    gamepad_queue_index = next;
    position->x = gamepad_queue_cells[gamepad_queue_index].x * canvas->window.width / canvas->scene.w;
    position->y = gamepad_queue_cells[gamepad_queue_index].y * canvas->window.height / canvas->scene.h;
    return true;
}

bool SCR_GamepadStatusPanelMove(int direction, vec2_t *position) {
    if (direction < 0 || direction > 3) return false;
    if (gamepad_panel_cursor_hero) {
        uint32_t index = gamepad_hero_index;
        if (!gamepad_hero_cell_count) gamepad_panel_cursor_hero = false;
        else {
            index = MIN(index, gamepad_hero_cell_count - 1);
        }
        if (gamepad_panel_cursor_hero && (direction == 0 || direction == 2)) {
            if (direction == 0 && index > 0) index--;
            else if (direction == 2 && index + 1 < gamepad_hero_cell_count) index++;
            else if (gamepad_status_cell_count) {
                uint32_t row = direction == 0
                    ? (gamepad_status_cell_count - 1) / gamepad_status_cells[0].columns : 0;
                uint32_t first = row * gamepad_status_cells[0].columns;
                uint32_t last = MIN(first + gamepad_status_cells[0].columns, gamepad_status_cell_count);
                uint32_t next = first;
                float best = 1e30f;
                FOR_LOOP(i, last - first) {
                    float dx = gamepad_status_cells[first + i].center.x - gamepad_hero_cells[index].center.x;
                    if (dx * dx < best) { best = dx * dx; next = first + i; }
                }
                gamepad_status_entity = gamepad_status_cells[next].entity;
                gamepad_panel_cursor_hero = false;
                return SCR_GamepadStatusCellScreenPosition(next, position);
            }
        }
        if (gamepad_panel_cursor_hero) {
            gamepad_hero_index = index;
            return SCR_GamepadHeroCellScreenPosition(index, position);
        }
    }
    if (!gamepad_status_cell_count) return false;
    uint32_t index = 0;
    bool found = false;
    FOR_LOOP(i, gamepad_status_cell_count) {
        if (gamepad_status_cells[i].entity == gamepad_status_entity) {
            index = i;
            found = true;
            break;
        }
    }
    if (!found) FOR_LOOP(i, gamepad_status_cell_count) {
        if (gamepad_status_cells[i].focused) { index = i; found = true; break; }
    }
    uint32_t columns = 1;
    /* numcolumns is retained with each captured cell row via the same panel's
     * row/column indices, so vertical moves preserve the current column. */
    uint32_t row = gamepad_status_cells[index].row;
    uint32_t column = gamepad_status_cells[index].column;
    uint32_t next = index;
    if (direction == 1) {
        uint32_t row_start = row * gamepad_status_cells[index].columns;
        uint32_t row_end = MIN(row_start + gamepad_status_cells[index].columns,
                               gamepad_status_cell_count);
        if (index + 1 < row_end) next = index + 1;
    } else if (direction == 3) {
        uint32_t row_start = row * gamepad_status_cells[index].columns;
        if (index > row_start) next = index - 1;
    }
    else {
        columns = gamepad_status_cells[index].columns;
        uint32_t last_row = (gamepad_status_cell_count - 1) / columns;
        if (direction == 0 && row == 0 && gamepad_hero_cell_count) {
            uint32_t hero = gamepad_hero_cell_count - 1;
            gamepad_hero_index = hero;
            gamepad_panel_cursor_hero = true;
            return SCR_GamepadHeroCellScreenPosition(hero, position);
        }
        if (direction == 2 && row == last_row && gamepad_hero_cell_count) {
            gamepad_hero_index = 0;
            gamepad_panel_cursor_hero = true;
            return SCR_GamepadHeroCellScreenPosition(0, position);
        }
        uint32_t target_row = direction == 0 ? (row ? row - 1 : row) : row + 1;
        uint32_t target_index = target_row * columns + column;
        if (target_index < gamepad_status_cell_count) next = target_index;
        else if (direction == 2 && target_row * columns < gamepad_status_cell_count)
            next = gamepad_status_cell_count - 1;
    }
    gamepad_status_entity = gamepad_status_cells[next].entity;
    return SCR_GamepadStatusCellScreenPosition(next, position);
}

void SCR_GamepadRadialOpen(void) {
    gamepad_radial_open = true;
    gamepad_radial_page = 0;
    gamepad_radial_sector = -1;
}
void SCR_GamepadRadialCancel(void) { gamepad_radial_open = false; gamepad_radial_sector = -1; }
bool SCR_GamepadRadialActive(void) { return gamepad_radial_open; }
void SCR_GamepadRadialNextPage(void) {
    if (!gamepad_radial_open) return;
    int pages = 1;
    FOR_LOOP(i, GAMEPAD_RADIAL_PAGES) FOR_LOOP(slot, GAMEPAD_RADIAL_SLOTS)
        if (gamepad_radial[i][slot].present) pages = MAX(pages, (int)i + 1);
    gamepad_radial_page = (gamepad_radial_page + 1) % pages;
}
void SCR_GamepadRadialSetAxis(float x, float y) {
    if (!gamepad_radial_open) return;
    float magnitude = x * x + y * y;
    if (magnitude < 0.09f) { gamepad_radial_sector = -1; return; }
    /* Cardinal directions select sector centers: up=0 (12 o'clock), down=5
     * (6 o'clock), right=2/3 and left=7/8. Diagonals choose nearest center. */
    float angle = atan2f(x, -y);
    if (angle < 0) angle += 2.0f * (float)M_PI;
    gamepad_radial_sector = ((int)floorf(angle * GAMEPAD_RADIAL_SLOTS /
                         (2.0f * (float)M_PI) + 0.5f)) % GAMEPAD_RADIAL_SLOTS;
}

static bool SCR_GamepadRadialOwnsSparkle(uint32_t frame_number) {
    FOR_LOOP(page, GAMEPAD_RADIAL_PAGES) FOR_LOOP(slot, GAMEPAD_RADIAL_SLOTS) {
        gamepadRadialItem_t const *item = &gamepad_radial[page][slot];
        if (item->sparkle_present && item->autocast_sparkle.number == frame_number) return true;
    }
    return false;
}
void SCR_GamepadSelectionCircle(vec2_t center, float radius, bool visible) {
    gamepad_selection_center = center;
    gamepad_selection_radius = radius;
    gamepad_selection_visible = visible;
}

bool SCR_GamepadNavigate(int direction, vec2_t from, vec2_t *to) {
    bool found = false;
    bool wrapped = false;
    float best = 1e30f;
    float wrap_best = 1e30f;
    uint32_t wrap_index = 0;
    bool modal = CL_WindowModalActive() || SCR_LayoutModalActive();
    if (layout_current_window) {
        gamepad_nav_count = 0;
        for (uint32_t i = 0; i < SCR_NumFrames(); ++i) {
            uiFrame_t const *frame = SCR_Frame(i);
            if (!frame || !SCR_LayoutFrameHasClickCommand(frame)) continue;
            rect_t const *rect = SCR_LayoutRect(frame);
            if (!rect) continue;
            if (gamepad_nav_count < GAMEPAD_NAV_MAX) {
                gamepad_nav[gamepad_nav_count++] = (typeof(*gamepad_nav)) {
                    { rect->x + rect->w * 0.5f, rect->y + rect->h * 0.5f }, true };
            }
        }
    }
    FOR_LOOP(i, gamepad_nav_count) {
        float dx = gamepad_nav[i].center.x - from.x;
        float dy = gamepad_nav[i].center.y - from.y;
        float parallel = direction == 0 ? -dy : direction == 1 ? dx :
                         direction == 2 ? dy : -dx;
        float orthogonal = (direction & 1) ? dy : dx;
        if (modal && !gamepad_nav[i].modal) continue;
        if (parallel < -GAMEPAD_NAV_MIN_STEP) {
            float const score = -parallel + 2.5f * fabsf(orthogonal);
            if (score < wrap_best) { wrap_best = score; wrap_index = i; }
            continue;
        }
        if (parallel <= GAMEPAD_NAV_MIN_STEP) continue;
        float score = parallel + 2.5f * fabsf(orthogonal);
        if (score < best) {
            best = score; *to = gamepad_nav[i].center; found = true;
        }
    }
    if (!found && wrap_best < 1e30f) {
        *to = gamepad_nav[wrap_index].center;
        found = wrapped = true;
    }
    return found;
}

static handle_t layout_layers[MAX_LAYOUT_LAYERS];
static texture_t *layout_dynamic_pics[MAX_DYNAMIC_IMAGES];
static char layout_dynamic_pic_names[MAX_DYNAMIC_IMAGES][512];
static uint32_t layout_dynamic_pic_cursor;
static bool layout_left_down;
/* Captured identity is layer+frame, since frame numbers repeat across layers. */
static uint32_t layout_press_frame, layout_press_layer;
static uint32_t layout_right_press_frame, layout_right_press_layer;
static char layout_held_command[CMDARG_LEN * 2];
static uint32_t layout_hovered_number;
static uint32_t layout_hovered_layer;
static uint32_t layout_current_layer;
static handle_t layout_hovered;
static handle_t layout_current;
static uintptr_t layout_current_window_camera_namespace;

/* Project a world point through the active camera into the virtual UI canvas.
 * The world scissor is authoritative: callers should not turn an off-screen
 * world event into a HUD notification pinned to the nearest edge. */
bool SCR_ProjectWorldPoint(vec3_t const *point, vec2_t *screen) {
    float const *m;
    float cx, cy, cw, vx, vy;

    if (!point || !screen) return false;
    m = cl.viewDef.viewProjectionMatrix.v;
    cx = m[0] * point->x + m[4] * point->y + m[8] * point->z + m[12];
    cy = m[1] * point->x + m[5] * point->y + m[9] * point->z + m[13];
    cw = m[3] * point->x + m[7] * point->y + m[11] * point->z + m[15];
    if (cw <= 0.0001f) return false;
    vx = cl.viewDef.viewport.x + (cx / cw * 0.5f + 0.5f) * cl.viewDef.viewport.w;
    vy = cl.viewDef.viewport.y + (cy / cw * 0.5f + 0.5f) * cl.viewDef.viewport.h;
    if (vx < cl.viewDef.scissor.x || vx > cl.viewDef.scissor.x + cl.viewDef.scissor.w ||
        vy < cl.viewDef.scissor.y || vy > cl.viewDef.scissor.y + cl.viewDef.scissor.h) return false;
    *screen = MAKE(vec2_t, vx * SCR_UICanvasWidth(), (1.0f - vy) * UI_BASE_HEIGHT);
    return true;
}

/* Entity-context layouts use a server-authored tree rooted at the client-projected model top. */
bool SCR_LayoutWorldHoverRoot(rect_t *root) {
    entityState_t const *ent = SCR_LayoutContextEntity();
    vec3_t top;
    vec2_t screen;

    if (!root || !ent) return false;
    FOR_LOOP(i, cl.viewDef.num_entities) {
        if (cl.viewDef.entities[i].number != cl.hover_entity) continue;
        if (!re.GetEntityOverheadPosition(&cl.viewDef.entities[i], &top)) return false;
        if (!SCR_ProjectWorldPoint(&top, &screen)) return false;
        *root = MAKE(rect_t, screen.x, screen.y, 0, 0);
        return true;
    }
    return false;
}

static rect_t get_uvrect(uint8_t const *tc) {
    return (rect_t){ tc[0], tc[2], tc[1]-tc[0], tc[3]-tc[2] };
}

static texture_t const *SCR_LayoutPic(RESOURCE image) {
    return image && image < MAX_IMAGES ? cl.pics[image] : NULL;
}

static texture_t const *SCR_LayoutGetDynamicTexture(cstring_t resource) {
    if (!resource || !*resource || !strcmp(resource, " ")) return NULL;

    uint32_t slot = MAX_DYNAMIC_IMAGES;
    FOR_LOOP(i, MAX_DYNAMIC_IMAGES) {
        if (layout_dynamic_pics[i] && !strcmp(layout_dynamic_pic_names[i], resource))
            return layout_dynamic_pics[i];
        if (!layout_dynamic_pics[i] && slot == MAX_DYNAMIC_IMAGES)
            slot = i;
    }
    if (slot == MAX_DYNAMIC_IMAGES) {
        slot = layout_dynamic_pic_cursor++ % MAX_DYNAMIC_IMAGES;
        SAFE_DELETE(layout_dynamic_pics[slot], re.ReleaseTexture);
        layout_dynamic_pic_names[slot][0] = '\0';
    }
    layout_dynamic_pics[slot] = re.LoadTexture(resource);
    if (!layout_dynamic_pics[slot]) return NULL;
    snprintf(layout_dynamic_pic_names[slot], sizeof(layout_dynamic_pic_names[slot]), "%s", resource);
    return layout_dynamic_pics[slot];
}

static rect_t scale_rect(rect_t const *r, float f) {
    float dx = r->w * (1-f), dy = r->h * (1-f);
    return (rect_t){ r->x + dx/2, r->y + dy/2, r->w - dx, r->h - dy };
}

/* A HUD draw can emit several command buttons (and nested window overlays).
 * Resolve its first selected entity once. The cache is deliberately scoped to
 * drawing, not cl.time: an unrelated draw or a new snapshot must never inherit
 * a stale selection, including a cached NULL result. */
static struct {
    uint32_t depth;
    bool resolved;
    entityState_t const *entity;
} layout_selected_cache;

static void SCR_BeginSelectedEntityDraw(void) {
    if (!layout_selected_cache.depth++) {
        layout_selected_cache.resolved = false;
        layout_selected_cache.entity = NULL;
    }
}

static void SCR_EndSelectedEntityDraw(void) {
    if (!--layout_selected_cache.depth) {
        layout_selected_cache.resolved = false;
        layout_selected_cache.entity = NULL;
    }
}

static entityState_t const *SCR_LayoutSelectedEntity(void) {
    entityState_t const *selected = NULL;
    if (layout_selected_cache.depth && layout_selected_cache.resolved)
        return layout_selected_cache.entity;

    FOR_LOOP(i, cl.num_entities) {
        entityState_t const *ent = &cl.ents[i].current;
        if (ent->renderfx & RF_SELECTED) {
            selected = ent;
            break;
        }
    }
    if (layout_selected_cache.depth) {
        layout_selected_cache.entity = selected;
        layout_selected_cache.resolved = true;
    }
    return selected;
}

void SCR_LayoutDrawSegmentedStatusbar(uiFrame_t const *frame, rect_t const *screen) {
    entityState_t const *ent;
    uint32_t count, capacity;
    float gap, width;
    rect_t const uv = { 0, 0, 1, 1 };

    if (!frame || !screen || !SCR_LayoutEntityContextActive()) return;
    ent = SCR_LayoutContextEntity();
    if (!ent || frame->stat >= ENT_STAT_COUNT || !frame->tex.index) return;
    count = EntityCargoCount(ent->stats[frame->stat]);
    capacity = EntityCargoCapacity(ent->stats[frame->stat]);
    if (!capacity) return;
    count = MIN(count, capacity);

    /* Segmented status bars use the primary art for filled slots and optional
     * secondary art for empty slots. Capacity owns the slot geometry even when
     * the current count is zero. */
    gap = MAX(0.0f, frame->value);
    width = (screen->w - gap * (float)(capacity - 1)) / (float)capacity;
    if (width <= 0.0f) return;

    FOR_LOOP(i, capacity) {
        bool const occupied = i < count;
        RESOURCE const image = occupied ? frame->tex.index : frame->tex.index2;
        rect_t segment = *screen;
        if (!image) continue;
        segment.x += (float)i * (width + gap);
        segment.w = width;
        re.DrawImage(cl.pics[image], &segment, &uv, occupied ? frame->color : COLOR32_WHITE);
    }
}

/* The game sends compact, authorized HUD vitals independently of world
 * snapshots. A controlled Hero can be out of sight without disappearing from
 * the shortcut HUD or revealing its world coordinates. */
static bool SCR_LayoutStatusbarValue(uiFrame_t const *frame, float *value) {
    if (!(frame->flagsvalue & UIFLAG_ENTITY_STATUSBAR)) {
        *value = frame->value;
        SCR_LayoutContextValue(frame->stat, value);
        return true;
    }
    if (frame->stat != ENT_HEALTH && frame->stat != ENT_MANA) return false;
    if (frame->value < 1.0f || frame->value >= MAX_CLIENT_ENTITIES) return false;
    uint32_t const number = (uint32_t)frame->value;
    if ((float)number != frame->value) return false;
    if (!cl.ui_entity_stats[number].valid) return false;
    if (frame->stat == ENT_MANA && !cl.ui_entity_stats[number].has_mana) return false;
    *value = BYTE2FLOAT(frame->stat == ENT_HEALTH ? cl.ui_entity_stats[number].hp : cl.ui_entity_stats[number].mana);
    return true;
}

/* Common health tint used by Warcraft III's percentage-driven life displays.
 * 0% red, 50% yellow, 100% green; interpolate in both halves. */
static color32_t SCR_HealthFractionColor(float fraction) {
    float const ratio = MAX(0.0f, MIN(1.0f, fraction));
    float const red = MIN(1.0f, (1.0f - ratio) * 2.0f);
    float const green = MIN(1.0f, ratio * 2.0f);
    return MAKE(color32_t, (uint8_t)(red * 255.0f + 0.5f),
                (uint8_t)(green * 255.0f + 0.5f), 0, 255);
}
void SCR_LayoutDrawStatusbar(uiFrame_t const *frame, rect_t const *screen) {
    rect_t const uv = { 0, 0, 255, 255 };
    rect_t screen2 = *screen, uv2 = uv;
    float value;
    if (!SCR_LayoutStatusbarValue(frame, &value)) return;
    color32_t const tint = (frame->flagsvalue & UIFLAG_HEALTH_GRADIENT)
        ? SCR_HealthFractionColor(value) : frame->color;
    if (frame->stat == UI_STAT_SELECTION_TIMED_STATUS && Cvar_Integer("ui_layout_debug", 0) >= 3) {
        static int last_bucket = -1;
        int const bucket = (int)(MAX(0.0f, MIN(1.0f, value)) * 20.0f);
        if (bucket != last_bucket || Cvar_Integer("ui_layout_debug", 0) >= 4) {
            fprintf(stderr,
                    "UI_TIMED_STATUS draw frame=%u layer=%u value=%.4f raw=%u rect=(%.4f,%.4f %.4fx%.4f) tex=%u border=%u\n",
                    (unsigned)frame->number, (unsigned)layout_current_layer, value,
                    (unsigned)cl.playerstate.stats[UI_PLAYERSTAT_SELECTION_TIMED_STATUS],
                    screen->x, screen->y, screen->w, screen->h,
                    (unsigned)frame->tex.index, (unsigned)frame->tex.index2);
            last_bucket = bucket;
        }
    }
    screen2.w *= value;
    uv2.w    *= value;
    rect_t const suv2 = Rect_div(&uv2, 0xff);
    re.DrawImage(cl.pics[frame->tex.index], &screen2, &suv2, tint);
    if (frame->tex.index2 > 0) {
        rect_t const suv = Rect_div(&uv, 0xff);
        re.DrawImage(cl.pics[frame->tex.index2], screen, &suv, COLOR32_WHITE);
    }
}

void SCR_LayoutDrawTexture(uiFrame_t const *frame, rect_t const *screen) {
    float value = 0;
    bool const has = SCR_LayoutContextValue(frame->stat, &value);
    bool const ctx = SCR_LayoutEntityContextActive() &&
                     (frame->stat == UI_STAT_CONTEXT_HEALTH || frame->stat == UI_STAT_CONTEXT_MANA);
    /* Mana hides by flag so an empty pool still draws the bar; health still
     * drops at value 0 because that snapshot means the unit is dead. */
    if (has ? (value <= 0.0f && frame->stat != UI_STAT_CONTEXT_MANA) : ctx) return;
    if (!frame->tex.index) return;  /* unresolved texture — skip to avoid drawing cl.pics[0] */
    texture_t const *tex = cl.pics[frame->tex.index];
    if (frame->stat >= MAX_STATS && frame->stat - MAX_STATS < PLAYERTEXT_COUNT) {
        cstring_t resource = cl.playerstate.texts[frame->stat - MAX_STATS];
        texture_t const *dyn = SCR_LayoutGetDynamicTexture(resource);
        if (dyn) tex = dyn;
    }
    if (frame->buffer.data && frame->buffer.size >= sizeof(uiTextureUV_t)) {
        uiTextureUV_t const *uv = frame->buffer.data;
        color32_t color = uv->color.a ? uv->color : frame->color;
        re.DrawImageEx(&MAKE(drawImage_t,
                             .texture = tex,
                             .shader = SHADER_UI,
                             .alphamode = uv->alphamode,
                             .screen = *screen,
                             .uv = MAKE(rect_t, uv->l, uv->t, uv->r - uv->l, uv->b - uv->t),
                             .color = color));
    } else {
        rect_t const uv = get_uvrect(frame->tex.coord);
        rect_t const suv = Rect_div(&uv, 0xff);
        re.DrawImage(tex, screen, &suv, frame->color);
    }
}

static void SCR_LayoutDrawHighlightData(uiHighlight_t const *h, rect_t const *screen) {
    texture_t const *texture;
    if (!h || !screen || !(texture = SCR_LayoutPic(h->alphaFile))) return;
    re.DrawImageEx(&MAKE(drawImage_t,
        .texture   = texture,
        .alphamode = h->alphaMode,
        .screen    = *screen,
        .uv        = MAKE(rect_t,0,0,1,1),
        .color     = COLOR32_WHITE,
        .shader    = SHADER_UI));
}

void SCR_LayoutDrawHighlight(uiFrame_t const *frame, rect_t const *screen) {
    SCR_LayoutDrawHighlightData(frame->buffer.data, screen);
}

static bool SCR_LayoutFrameIsHovered(uiFrame_t const *frame);

void SCR_LayoutSimpleButton(uiFrame_t const *frame, rect_t const *screen) {
    uiSimpleButton_t const *b = frame->buffer.data;
    bool const enabled = SCR_LayoutFrameHasClickCommand(frame);
    bool const hovered = SCR_LayoutFrameIsHovered(frame);
    bool const pushed = enabled && hovered && layout_left_down;
    uiSimpleButtonState_t const *state = !enabled ? &b->disabled : pushed ? &b->pushed : &b->normal;
    texture_t const *texture = SCR_LayoutPic(state->texture);
    if (!texture) {
        state = &b->normal;
        texture = SCR_LayoutPic(state->texture);
    }
    if (texture) {
        rect_t const uv = get_uvrect((uint8_t *)&state->texcoord);
        rect_t const suv = Rect_div(&uv, 0xff);
        re.DrawImage(texture, screen, &suv, COLOR32_WHITE);
    }
    re.DrawText(&MAKE(drawText_t,
        .rect      = *screen,
        .font      = cl.fonts[state->font],
        .text      = frame->text,
        .color     = state->fontcolor,
        .textWidth = screen->w));
}

void SCR_LayoutDrawBackdrop2(uiFrame_t const *frame, rect_t const *screen, uiBackdrop_t const *bd) {
    texture_t const *background, *edge;
    if (!bd || !screen || screen->w <= 0 || screen->h <= 0) return;
    background = SCR_LayoutPic(bd->Background);
    edge = SCR_LayoutPic(bd->EdgeFile);
    if (!background && !edge) return;
    re.DrawBackdrop(&MAKE(drawBackdrop_t,
        .screen        = *screen,
        .bg.texture    = background,
        .bg.color      = frame->color,
        .backgroundSize = bd->BackgroundSize,
        .edge.texture  = edge,
        .edge.color    = frame->color,
        .corner.flags  = bd->CornerFlags,
        .corner.size   = bd->CornerSize,
        .insets.right  = bd->BackgroundInsets[0],
        .insets.top    = bd->BackgroundInsets[1],
        .insets.bottom = bd->BackgroundInsets[2],
        .insets.left   = bd->BackgroundInsets[3],
        .flags = (bd->TileBackground ? DRAW_TILE     : 0)
               | (bd->Mirrored       ? DRAW_MIRRORED : 0)
               | (bd->Opaque         ? DRAW_BG_OPAQUE : 0)));
}

void SCR_LayoutDrawBackdrop(uiFrame_t const *frame, rect_t const *screen) {
    SCR_LayoutDrawBackdrop2(frame, screen, frame->buffer.data);
}

static bool SCR_LayoutBackdropHasArt(uiBackdrop_t const *bd) {
    return bd && (bd->Background || bd->EdgeFile);
}

static void SCR_LayoutDrawBackdropPart(uiFrame_t const *frame, rect_t const *screen, uiBackdrop_t const *bd) {
    if (SCR_LayoutBackdropHasArt(bd) && screen->w > 0 && screen->h > 0)
        SCR_LayoutDrawBackdrop2(frame, screen, bd);
}

/* WoW sliders use compact cropped textures; retain backdrop drawing for legacy FDF scrollbars. */
static bool SCR_LayoutDrawScrollImage(RESOURCE texture, uint8_t const *texcoord, rect_t const *screen) {
    rect_t uv, suv;
    texture_t const *image = SCR_LayoutPic(texture);
    if (!image) return false;
    uv = get_uvrect(texcoord); suv = Rect_div(&uv, 0xff);
    re.DrawImage(image, screen, &suv, COLOR32_WHITE);
    return true;
}


static uiFrame_t const *SCR_LayoutTextAreaScrollBar(uiFrame_t const *frame) {
    if (!frame || !frame->number || frame->flags.type != FT_TEXTAREA) return NULL;
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *child = SCR_Frame(i);
        if (child && child->parent == frame->number && child->flags.type == FT_SCROLLBAR)
            return child;
    }
    return NULL;
}

static rect_t SCR_LayoutTextAreaView(uiFrame_t const *frame, rect_t const *screen) {
    uiTextArea_t const *ta = frame && frame->buffer.data ? frame->buffer.data : NULL;
    float inset = ta ? ta->inset : 0.0f;
    rect_t view = { screen->x + inset, screen->y + inset,
                  MAX(0.0f, screen->w - inset * 2), MAX(0.0f, screen->h - inset * 2) };
    uiFrame_t const *scrollbar = SCR_LayoutTextAreaScrollBar(frame);
    if (scrollbar) {
        float sw = SCR_LayoutRect(scrollbar)->w;
        if (sw > 0.0f && sw < view.w) view.w = MAX(0.0f, view.w - sw);
    }
    return view;
}

float SCR_LayoutTextAreaMaxScroll(uiFrame_t const *frame) {
    uiTextArea_t const *ta;
    rect_t view;
    drawText_t measure;
    cstring_t value;

    if (!frame || frame->flags.type != FT_TEXTAREA || !frame->buffer.data || !re.GetTextSize)
        return 0.0f;
    ta = frame->buffer.data;
    view = SCR_LayoutTextAreaView(frame, SCR_LayoutRect(frame));
    if (view.w <= 0.0f || view.h <= 0.0f) return 0.0f;
    value = SCR_GetStringValue(frame);
    measure = SCR_GetDrawText(frame, view.w, value ? value : "", &(uiLabel_t){
        .font = ta->font, .textalignx = FONT_JUSTIFYLEFT, .textaligny = FONT_JUSTIFYTOP });
    measure.flags |= DRAW_WORD_WRAP;
    return MAX(0.0f, re.GetTextSize(&measure).y - view.h);
}

static int SCR_LayoutListBoxVisibleRows(uiFrame_t const *frame, rect_t const *view) {
    uiListBox_t const *lb;
    float item_height;

    if (!frame || !view || frame->flags.type != FT_LISTBOX || !frame->buffer.data ||
        frame->buffer.size < sizeof(uiListBox_t) || view->h <= 0.0f) return 0;
    lb = frame->buffer.data;
    item_height = lb->itemHeight > 0.0f ? lb->itemHeight : 0.018f;
    return MAX((int)floorf(view->h / item_height), 1);
}

static int SCR_LayoutListBoxMaxScroll(uiFrame_t const *frame) {
    uiListBox_t const *lb;
    rect_t view;
    int count = 0, visible;

    if (!frame || frame->flags.type != FT_LISTBOX || !frame->buffer.data ||
        frame->buffer.size < sizeof(uiListBox_t)) return 0;
    lb = frame->buffer.data;
    view = Rect_inset(SCR_LayoutRect(frame), lb->border);
    if (!strcmp(lb->fetchCommand, "FRAME_ROWS")) {
        FOR_LOOP(i, SCR_NumFrames()) {
            uiFrame_t const *child = SCR_Frame(i);
            if (child && child->parent == frame->number && child->flags.type != FT_SCROLLBAR) count++;
        }
    } else if (frame->text && *frame->text) {
        count = 1;
        for (cstring_t p = frame->text; *p; p++) if (*p == '\n') count++;
    }
    visible = SCR_LayoutListBoxVisibleRows(frame, &view);
    return MAX(count - visible, 0);
}

void SCR_LayoutDrawScrollBar(uiFrame_t const *frame, rect_t const *screen) {
    uiScrollBarImage_t const *art = frame->buffer.size == sizeof(*art) ? frame->buffer.data : NULL;
    uiScrollBar_t const *sb = !art && frame->buffer.size >= sizeof(*sb) ? frame->buffer.data : NULL;
    uiFrame_t const *parent = frame->parent < SCR_NumFrames() ? SCR_Frame(frame->parent) : NULL;
    if ((!art && !sb) || screen->w <= 0 || screen->h <= 0) return;

    if (frame->flags.type == FT_SLIDER) {
        float const fraction = MIN(MAX(frame->value, 0.0f), 1.0f);
        float const thumb_w = MIN(screen->w, MAX(screen->h * UI_PIXEL_ASPECT, 0.010f));
        rect_t thumb = {
            screen->x + (screen->w - thumb_w) * fraction,
            screen->y,
            thumb_w,
            screen->h,
        };
        if (sb) {
            SCR_LayoutDrawBackdropPart(frame, screen, &sb->background);
            SCR_LayoutDrawBackdropPart(frame, &thumb, &sb->thumbButton);
        } else {
            SCR_LayoutDrawScrollImage(art->image[2], art->texcoord, &thumb);
        }
        return;
    }

    if (parent && parent->flags.type == FT_TEXTAREA) {
        if (SCR_LayoutTextAreaMaxScroll(parent) <= 0.0f) return;
        ((uiFrame_t *)frame)->value = parent->value;
    } else if (parent && parent->flags.type == FT_LISTBOX) {
        if (SCR_LayoutListBoxMaxScroll(parent) <= 0) return;
        ((uiFrame_t *)frame)->value = parent->value;
    }

    if (sb) SCR_LayoutDrawBackdropPart(frame, screen, &sb->background);

    /* Equal authored pixel dimensions need a taller Y span in WoW's normalized square UI scene. */
    float bh = MIN(screen->w * UI_PIXEL_ASPECT, screen->h * 0.5f);
    rect_t inc   = MAKE(rect_t, screen->x, screen->y + screen->h - bh, screen->w, bh);
    rect_t dec   = MAKE(rect_t, screen->x, screen->y, screen->w, bh);
    rect_t track = MAKE(rect_t, screen->x, dec.y + dec.h, screen->w, inc.y - (dec.y + dec.h));
    if (art) {
        SCR_LayoutDrawScrollImage(art->image[0], art->texcoord, &inc);
        SCR_LayoutDrawScrollImage(art->image[1], art->texcoord, &dec);
    } else {
        SCR_LayoutDrawBackdropPart(frame, &inc, &sb->incButton);
        SCR_LayoutDrawBackdropPart(frame, &dec, &sb->decButton);
    }
    if (track.h <= 0) return;

#ifdef UI_STRETCHED_SCROLLBAR_THUMB
    float th = MIN(MAX(bh, track.h * 0.25f), track.h);
#else
    float th = MIN(art ? bh : MIN(bh, 0.010f), track.h);
#endif
    float tw = art ? screen->w : MIN(screen->w, 0.010f);
    rect_t thumb = {
        screen->x + (screen->w - tw) * 0.5f,
        /* Slider value zero is the top of a top-origin UI; the old formula inverted it. */
        track.y + (track.h - th) * MIN(MAX(frame->value, 0.0f), 1.0f),
        tw, th
    };
    if (art) SCR_LayoutDrawScrollImage(art->image[2], art->texcoord, &thumb);
    else SCR_LayoutDrawBackdropPart(frame, &thumb, &sb->thumbButton);
}

bool SCR_LayoutFrameHasClickCommand(uiFrame_t const *frame) {
    return frame && !(frame->flagsvalue & UIFRAME_DISABLED) && frame->onclick && *frame->onclick;
}
static bool SCR_LayoutGlueTextButtonIsPushed(uiFrame_t const *frame) {
    /* The left button is global, but the pushed state belongs only to the hovered layout frame. */
    return layout_left_down && SCR_LayoutFrameHasClickCommand(frame) && SCR_LayoutFrameIsHovered(frame);
}
static bool SCR_LayoutFrameIsHovered(uiFrame_t const *frame) {
    if (!frame || frame->number != layout_hovered_number) return false;
    /* Transient windows identify the hovered layout by handle. Persistent HUD
     * layers identify it by layer number; layout_current is also used while
     * drawing those layers, so it must not make ordinary HUD hover look like a
     * window hover. */
    if (layout_hovered)
        return layout_current_window && layout_current == layout_hovered;
    return !layout_current_window && layout_current_layer == layout_hovered_layer;
}

static void SCR_LayoutFormatOnClickCommand(cstring_t src, string_t dst, uint32_t dsz) {
    if (!dst || dsz == 0) return;
    dst[0] = '\0';
    if (!src) return;

    uint32_t out = 0;
    for (uint32_t i = 0; src[i] && out + 1 < dsz; i++) {
        if (src[i] == '{') {
            char name[80]; uint32_t nlen = 0;
            uint32_t j = i + 1;
            while (src[j] && src[j] != '}' && nlen + 1 < sizeof(name))
                name[nlen++] = src[j++];
            if (src[j] == '}') {
                char val[16];
                name[nlen] = '\0';
                snprintf(val, sizeof(val), "%d", 0);
                for (uint32_t k = 0; val[k] && out + 1 < dsz; k++)
                    dst[out++] = val[k];
                i = j;
                continue;
            }
        }
        dst[out++] = src[i];
    }
    dst[out] = '\0';
}

void SCR_LayoutSendFrameCommand(uiFrame_t const *frame) {
    char command[CMDARG_LEN * 2];
    if (!SCR_LayoutFrameHasClickCommand(frame)) return;
    SCR_LayoutFormatOnClickCommand(frame->onclick, command, sizeof(command));
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s", command);
}

void SCR_LayoutSetPointer(handle_t layout, uint32_t number, bool down) {
    layout_hovered = layout;
    layout_hovered_number = number;
    layout_left_down = down;
}

void SCR_WindowPrepare(handle_t layout, rect_t const *root) {
    layout_current_window = true;
    layout_current = layout;
    layout_current_window_camera_namespace = 0;
    SCR_ClearWindow(layout);
    if (root) SCR_SetLayoutRoot(root);
}

void SCR_WindowSetCameraNamespace(uintptr_t window_namespace) {
    layout_current_window_camera_namespace = window_namespace;
}

bool SCR_WindowLayoutIsCurrent(handle_t layout) {
    return layout && layout_current_window && layout_current == layout;
}

static void SCR_LayoutCheckBox(uiFrame_t const *frame, rect_t const *screen) {
    uiCheckBox_t const *cb = frame->buffer.data;
    bool const enabled = SCR_LayoutFrameHasClickCommand(frame);
    bool const pushed = enabled && SCR_LayoutFrameIsHovered(frame) && layout_left_down;
    uiBackdrop_t const *bd = enabled
        ? (pushed ? &cb->pushed : &cb->normal)
        : (pushed ? &cb->disabledPushed : &cb->disabled);

    SCR_LayoutDrawBackdrop2(frame, screen, bd);
    if (frame->value >= 0.5f)
        SCR_LayoutDrawHighlightData(enabled ? &cb->checked : &cb->disabledChecked, screen);
    if (enabled && SCR_LayoutFrameIsHovered(frame))
        SCR_LayoutDrawHighlightData(&cb->mouseOver, screen);
}

void SCR_LayoutGlueTextButton(uiFrame_t const *frame, rect_t const *screen) {
    uiGlueTextButton_t const *gb = frame->buffer.data;
    bool const enabled = SCR_LayoutFrameHasClickCommand(frame);
    bool const pushed  = SCR_LayoutGlueTextButtonIsPushed(frame);
    uiBackdrop_t const *bd = enabled
        ? (pushed ? &gb->pushed : &gb->normal)
        : (pushed ? &gb->disabledPushed : &gb->disabled);
    SCR_LayoutDrawBackdrop2(frame, screen, bd);
}

static void SCR_LayoutDrawGlueTextButtonHighlight(uiFrame_t const *frame) {
    uiGlueTextButton_t const *gb = frame->buffer.data;
    if (SCR_LayoutFrameHasClickCommand(frame) && SCR_LayoutFrameIsHovered(frame))
        SCR_LayoutDrawHighlightData(&gb->highlight, SCR_LayoutRect(frame));
}

bool SCR_LayoutScrollTextAreaAt(handle_t layout, vec2_t const *point, int wheel_y) {
    (void)layout;
    if (!point || !wheel_y) return false;
    for (uint32_t i = SCR_NumFrames(); i > 0; i--) {
        uiFrame_t *frame = SCR_Frame(i - 1);
        if (!frame || frame->flags.type != FT_TEXTAREA ||
            !Rect_contains(SCR_LayoutRect(frame), point)) continue;
        if (SCR_LayoutTextAreaMaxScroll(frame) > 0.0f)
            frame->value = MIN(1.0f, MAX(0.0f, frame->value - wheel_y * 0.1f));
        return true;
    }
    return false;
}

void SCR_LayoutDrawBuildQueue(uiFrame_t const *frame, rect_t const *scrn) {
    rect_t screen = *scrn;
    rect_t const uv = { 0, 0, 1, 1 };
    uiBuildQueue_t const *queue = frame->buffer.data;
    bool const food_blocked = queue->numitems && queue->items[0].starttime == 0 && queue->items[0].endtime == 0;
    uint32_t active = food_blocked ? 0 : queue->numitems;

    if (!food_blocked) {
        FOR_LOOP(i, queue->numitems) {
            if (cl.time < queue->items[i].endtime) { active = i; break; }
        }
    }
    for (uint32_t i = active + 1; i < queue->numitems; i++) {
        if (food_blocked || cl.time < queue->items[i].endtime) {
            re.DrawImage(cl.pics[queue->items[i].image], &screen, &uv, frame->color);
            screen.x += queue->itemoffset;
        }
    }
}

static void SCR_LayoutDrawMessageQueue(uiFrame_t const *frame, rect_t const *screen) {
    uiMessageQueue_t const *message = frame->buffer.data;
    rect_t uv = MAKE(rect_t, 0, 0, 0.53125f, 0.6875f);
    if (!message || frame->buffer.size < sizeof(*message)) {
        fprintf(stderr, "SCR_LayoutDrawMessageQueue: invalid payload size %u\n", (unsigned)frame->buffer.size);
        return;
    }
    if (message->flags & UI_MESSAGE_UNREAD) {
        texture_t const *icon = SCR_LayoutPic(message->image);
        if (icon) re.DrawImage(icon, screen, &uv, COLOR32_WHITE);
    }
    if (message->flags & UI_MESSAGE_OPEN) {
        rect_t title = MAKE(rect_t, screen->x + 0.04f, screen->y + 0.02f, screen->w - 0.08f, 0.04f);
        rect_t body = MAKE(rect_t, screen->x + 0.06f, screen->y + 0.07f, screen->w - 0.12f, screen->h - 0.09f);
        re.DrawFill(screen, MAKE(color32_t, 10, 8, 5, 245));
        re.DrawText(&MAKE(drawText_t, .font = cl.fonts[message->title_font], .text = frame->text,
                          .rect = title, .color = MAKE(color32_t, 255, 215, 120, 255),
                          .textWidth = title.w, .halign = FONT_JUSTIFYCENTER, .valign = FONT_JUSTIFYMIDDLE));
        re.DrawText(&MAKE(drawText_t, .font = cl.fonts[message->body_font], .text = frame->tooltip,
                          .rect = body, .color = MAKE(color32_t, 240, 230, 205, 255),
                          .textWidth = body.w, .lineHeight = body.h, .flags = DRAW_WORD_WRAP,
                          .halign = FONT_JUSTIFYLEFT, .valign = FONT_JUSTIFYTOP));
    }
}

void SCR_LayoutUpdateBuildQueue(uiFrame_t const *frame, rect_t const *screen) {
    uiBuildQueue_t const *queue = frame->buffer.data;
    uiFrame_t *buildtimer = SCR_Frame(queue->buildtimer);
    uiFrame_t *firstitem  = SCR_Frame(queue->firstitem);

    if (queue->numitems && queue->items[0].starttime == 0 && queue->items[0].endtime == 0) {
        if (buildtimer) buildtimer->value = 0;
        if (firstitem) firstitem->tex.index = queue->items[0].image;
        return;
    }
    FOR_LOOP(i, queue->numitems) {
        uiBuildQueueItem_t const *item = &queue->items[i];
        if (item->flags & UI_BUILDQUEUE_ITEM_PAUSED) {
            if (buildtimer) buildtimer->value = MAX(0, MIN(item->progress, 1));
            if (firstitem) firstitem->tex.index = item->image;
            break;
        }
        if (cl.time < item->endtime) {
            float dur  = item->endtime - item->starttime;
            float elap = cl.time > item->starttime ? (float)(cl.time - item->starttime) : 0;
            float prog = MAX(0, MIN(dur > 0 ? elap / dur : 1, 1));
            if (buildtimer) buildtimer->value  = prog;
            if (firstitem)  firstitem->tex.index = item->image;
            break;
        }
    }
    (void)screen;
}

#define HP_BAR_HEIGHT_RATIO  0.175f
#define HP_BAR_SPACING_RATIO 0.02f

static uint32_t SCR_LayoutMultiselectEntityAt(uiFrame_t const *frame, vec2_t const *point) {
    uiMultiselect_t const *ms;
    uint32_t count;

    if (!frame || frame->flags.type != FT_MULTISELECT || !point ||
        !frame->buffer.data || frame->buffer.size < sizeof(uiMultiselect_t)) {
        return 0;
    }
    ms = frame->buffer.data;
    if (!ms->numcolumns) return 0;
    count = (frame->buffer.size - sizeof(uiMultiselect_t)) / sizeof(uiMultiselectItem_t);
    count = MIN((uint32_t)ms->numitems, count);
    FOR_LOOP(i, count) {
        rect_t cell = *SCR_LayoutRect(frame);
        uint32_t column = i % ms->numcolumns;
        uint32_t row = i / ms->numcolumns;

        cell.x += ms->offset.x * column;
        cell.y += ms->offset.y * row;
        if (Rect_contains(&cell, point)) return ms->items[i].entity;
    }
    return 0;
}

void SCR_LayoutDrawMultiSelect(uiFrame_t const *frame, rect_t const *scrn) {
    rect_t screen = *scrn;
    uiMultiselect_t const *ms = frame->buffer.data;
    uint32_t column = 0;
    FOR_LOOP(i, ms->numitems) {
        rect_t uv = { 0, 0, 1, 1 };
        uiMultiselectItem_t const *item = &ms->items[i];
        if ((item->flags & UI_MULTISELECT_ITEM_FOCUSED) && ms->focus_highlight) {
            rect_t highlight = {
                screen.x - screen.w * 0.185f,
                screen.y - screen.h * 0.10f,
                screen.w * 1.37f,
                screen.h * 1.75f
            };
            re.DrawImage(cl.pics[ms->focus_highlight], &highlight, &uv,
                         MAKE(color32_t, 255, 255, 0, 255));
        }
        re.DrawImage(cl.pics[item->image], &screen, &uv, frame->color);
        entityState_t const *ent = &cl.ents[item->entity].current;
        if (ent) {
            float hp   = BYTE2FLOAT(ent->stats[ENT_HEALTH]);
            float mana = BYTE2FLOAT(ent->stats[ENT_MANA]);
            rect_t rect  = { screen.x, screen.y + screen.h * (1 + HP_BAR_SPACING_RATIO),
                           screen.w * hp, screen.h * HP_BAR_HEIGHT_RATIO };
            uv.w = hp;
            re.DrawImage(cl.pics[ms->hp_bar], &rect, &uv, SCR_HealthFractionColor(hp));
            uv.w  = mana; rect.w  = screen.w * mana;
            rect.y += screen.h * (HP_BAR_HEIGHT_RATIO + HP_BAR_SPACING_RATIO);
            re.DrawImage(cl.pics[ms->mana_bar], &rect, &uv, MAKE(color32_t,0,255,255,255));
        }
        if (++column >= ms->numcolumns) {
            column   = 0;
            screen.x = SCR_LayoutRect(frame)->x;
            screen.y += ms->offset.y;
        } else {
            screen.x += ms->offset.x;
        }
    }
}

void SCR_LayoutDrawPortrait(uiFrame_t const *frame, rect_t const *screen) {
    float const canvas_w = SCR_UICanvasWidth();
    rect_t const viewport = {
        screen->x / canvas_w,
        (UI_BASE_HEIGHT - screen->y - screen->h) / UI_BASE_HEIGHT,
        screen->w / canvas_w,
        screen->h / UI_BASE_HEIGHT
    };
    uint32_t const model_index = frame->tex.index;
    model_t const *port = model_index < MAX_MODELS ? cl.portraits[model_index] : NULL;
    model_t const *model = model_index < MAX_MODELS ? cl.models[model_index] : NULL;
    model_t const *draw  = port ? port : model;
    cstring_t anim = (frame->text && *frame->text) ? frame->text : "Portrait";
    if (!draw) return;

    renderEntity_t entity = {0};
    entity.model = draw; entity.scale = 1.0f;
    entity.instance_id = layout_current_window
        ? (layout_current_window_camera_namespace
            ? (((uintptr_t)1 << (sizeof(uintptr_t) * 8 - 1)) |
               (layout_current_window_camera_namespace << 10) | frame->number)
            : 0)
        : (((uintptr_t)1 << (sizeof(uintptr_t) * 8 - 1)) |
           ((uintptr_t)layout_current_layer << 10) | frame->number);
    entity.team = frame->stat;
    entity.flags = RF_NO_SHADOW | RF_NO_FOGOFWAR | RF_PORTRAIT_LIGHTING;
    re.SetEntityAnimFrame(draw, anim, &entity);

    viewDef_t vd = {0};
    vd.viewport     = viewport;
    vd.scissor      = viewport;
    vd.rdflags      = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG |
                      RDF_USE_ENTITY_CAMERA | RDF_ISOLATED_PARTICLES;
    vd.num_entities = 1;
    vd.entities     = &entity;
    if (frame->buffer.size == sizeof(uiModel_t)) {
        size2_t size = re.GetWindowSize();
        float aspect = viewport.w * size.width / (viewport.h * size.height);
        /* The layout camera replaces the old radius guess and Stand-name mode switch. */
        M_ModelMatrix(frame->buffer.data, aspect, &vd.viewProjectionMatrix);
        Matrix4_identity(&vd.textureMatrix);
        Matrix4_identity(&vd.lightMatrix);
        vd.rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG | RDF_NOPARTICLES;
    }
    re.RenderFrame(&vd);
}

/* Loading bars are the one client-owned layout value; their art remains server-authored. */
void SCR_LayoutDrawLoadingBar(uiFrame_t const *frame, rect_t const *screen) {
    rect_t fill = *screen, uv = { 0, 0, 255, 255 };
    fill.w *= cl.loading_progress;
    uv.w *= cl.loading_progress;
    rect_t suv = Rect_div(&uv, 0xff);
    /* This frame declares an image. Model and image indices are independent namespaces. */
    re.DrawImage(SCR_LayoutPic(frame->tex.index), &fill, &suv, frame->color);
}

static void SCR_LayoutDrawSpriteWithScope(uiFrame_t const *frame, rect_t const *screen,
                                         void const *scope) {
    model_t const *model = cl.models[frame->tex.index];
    cstring_t anim = (frame->text && *frame->text) ? frame->text : "Stand";
    char sequence_anim[96];
    char phased_anim[96];
    float phase = 0.0f;

    /* Some server-authored sprites need one replicated stat for their
     * normalized animation phase and another for the authored sequence.  The
     * latter is opt-in because frame.value has unrelated meanings for other
     * frame types.  Only explicit #N selectors are rewritten. */
    if ((frame->flagsvalue & UIFLAG_SPRITE_STAT_SEQUENCE) &&
        frame->value > 0.0f && frame->value < (float)MAX_STATS && anim[0] == '#')
    {
        uint32_t const sequence_stat = (uint32_t)frame->value;
        cstring_t marker = strchr(anim, '@');
        if (marker) {
            snprintf(sequence_anim, sizeof(sequence_anim), "#%u%s",
                     (unsigned)cl.playerstate.stats[sequence_stat], marker);
        } else {
            snprintf(sequence_anim, sizeof(sequence_anim), "#%u",
                     (unsigned)cl.playerstate.stats[sequence_stat]);
        }
        anim = sequence_anim;
    }

    /* Sprite phase uses either a normalized snapshot stat or a generic local binding.
     * Loading sprites previously lost their geometry when converted into portrait bars. */
    if (SCR_LayoutContextValue(frame->stat, &phase) || (frame->stat > 0 && frame->stat < MAX_STATS)) {
        cstring_t marker = strchr(anim, '@');
        size_t base_len = marker ? (size_t)(marker - anim) : strlen(anim);
        if (frame->stat > 0 && frame->stat < MAX_STATS)
            phase = (float)cl.playerstate.stats[frame->stat] / (float)UINT16_MAX;

        if (base_len > sizeof(phased_anim) - 16) base_len = sizeof(phased_anim) - 16;
        snprintf(phased_anim, sizeof(phased_anim), "%.*s@%.6f", (int)base_len, anim, phase);
        anim = phased_anim;
    }
    re.DrawSprite(&MAKE(drawSprite_t, .model = model, .anim = anim, .x = screen->x, .y = screen->y,
        .id = frame, .scope = scope));
}

void SCR_LayoutDrawSprite(uiFrame_t const *frame, rect_t const *screen) {
    if (gamepad_radial_open && frame && SCR_GamepadRadialOwnsSparkle(frame->number)) return;
    SCR_LayoutDrawSpriteWithScope(frame, screen,
        layout_current_window ? layout_current : (void *)(uintptr_t)(layout_current_layer + 1));
}

/* Resolve the generic transient command-button alert tint from an absolute client/server clock deadline. */
static color32_t SCR_CommandButtonColor(uiFrame_t const *frame) {
    color32_t color = frame ? frame->color : COLOR32_WHITE;
    uint32_t deadline;
    float phase, pulse;

    if (!frame || !(frame->flagsvalue & UIFLAG_ALERT_RED_PULSE) || frame->value <= 0.0f) return color;
    deadline = (uint32_t)frame->value;
    if ((int32_t)(cl.time - deadline) >= 0) return color;

    phase = (float)(cl.time % (SCR_ALERT_PULSE_HALF_MS * 2)) / (float)SCR_ALERT_PULSE_HALF_MS;
    pulse = 1.0f - fabsf(phase - 1.0f);
    color.g = color.b = (uint8_t)(255.0f - pulse * (255.0f - SCR_ALERT_PULSE_MIN_GB));
    return color;
}

static float SCR_CommandButtonRadialShade(uiFrame_t const *frame) {
    uiCommandButton_t const *state;
    uint32_t duration;

    if (!frame || !(frame->flagsvalue & UIFLAG_RADIAL_SHADE) ||
        frame->buffer.size != sizeof(uiCommandButton_t) || !frame->buffer.data) return 0.0f;
    state = frame->buffer.data;
    duration = (uint32_t)(state->radialEndTime - state->radialStartTime);
    if (!duration || (int32_t)(cl.time - state->radialEndTime) >= 0) return 0.0f;
    if ((int32_t)(cl.time - state->radialStartTime) <= 0) return 1.0f;
    return MIN(1.0f, (float)(uint32_t)(state->radialEndTime - cl.time) / (float)duration);
}

void SCR_LayoutDrawCommandButton(uiFrame_t const *frame, rect_t const *screen) {
    /* The radial overlay replaces the square command-card grid while open. */
    if (gamepad_radial_open && !(frame->flagsvalue & UIFLAG_GAMEPAD_HUD_SHORTCUT)) return;
    entityState_t const *sel = SCR_LayoutSelectedEntity();
    bool const alternate_active = (frame->flagsvalue & UIFLAG_ALTERNATE_ACTIVE) != 0;
    bool const ability_match = sel && frame->stat != UINT8_MAX && sel->ability == frame->stat;
    bool const active_glow = alternate_active || ability_match ||
                             (frame->flagsvalue & UIFLAG_ABILITY_ENGAGED) != 0;
    rect_t const uv = get_uvrect(frame->tex.coord);
    rect_t const suv = Rect_div(&uv, 0xff);
    rect_t scrn = scale_rect(screen, SCR_LayoutFrameIsHovered(frame) && layout_left_down ? 0.875f : 0.925f);
    re.DrawImageEx(&MAKE(drawImage_t,
        .texture     = cl.pics[frame->tex.index],
        .screen      = scrn,
        .uv          = suv,
        .color       = SCR_CommandButtonColor(frame),
        .shader      = SHADER_COMMANDBUTTON,
        .uActiveGlow = active_glow,
        .uRadialShade = SCR_CommandButtonRadialShade(frame)));
}

void layout_text(uiFrame_t const *frame, rect_t const *screen, cstring_t text) {
    drawText_t dt = SCR_GetDrawText(frame, screen->w, text, frame->buffer.data);
    dt.rect   = *screen;
    dt.flags |= DRAW_WORD_WRAP;
    re.DrawText(&dt);
}

static void SCR_LayoutApplyPushedTextOffset(uiFrame_t const *frame, rect_t *screen) {
    if (frame->parent >= SCR_NumFrames()) return;
    uiFrame_t const *parent = SCR_Frame(frame->parent);
    if (!parent) return;
    if (parent->flags.type != FT_GLUETEXTBUTTON && parent->flags.type != FT_GLUEBUTTON) return;
    if (!SCR_LayoutFrameHasClickCommand(parent) || !SCR_LayoutGlueTextButtonIsPushed(parent)) return;
    uiGlueTextButton_t const *b = parent->buffer.data;
    screen->x += b->pushedTextOffset.x;
    screen->y -= b->pushedTextOffset.y;
}

void SCR_LayoutDrawString(uiFrame_t const *frame, rect_t const *screen) {
    uiLabel_t const *label = frame->buffer.data;
    cstring_t value = SCR_GetStringValue(frame);
    uint32_t cursor = 0;

    /* Transient edit boxes draw their live value from the parent control.
     * Keeping the child STRING/TEXT only as the authored geometry carrier
     * avoids relying on generic label rendering for edit-control text. */
    if (frame->parent < SCR_NumFrames()) {
        uiFrame_t const *parent = SCR_Frame(frame->parent);
        if (parent && (parent->flags.type == FT_EDITBOX ||
                       parent->flags.type == FT_GLUEEDITBOX ||
                       parent->flags.type == FT_SLASHCHATBOX))
            return;
    }
    /* Label offsets use the same fixed-point wire representation as anchors. */
    rect_t scr = { screen->x + label->offsetx / UI_FRAMEPOINT_SCALE,
                 screen->y + label->offsety / UI_FRAMEPOINT_SCALE, screen->w, screen->h };
    SCR_LayoutApplyPushedTextOffset(frame, &scr);
    layout_text(frame, &scr, value);
    if (CL_WindowEditCursor(frame->number, &cursor) && re.GetTextSize) {
        drawText_t dt = SCR_GetDrawText(frame, scr.w, value, frame->buffer.data);
        dt.rect = scr;
        M_DrawTextInputCursor(&re, &dt, value, cursor, COLOR32_WHITE);
    }
}

/* Draw a nameplate whose backdrop and text share the measured content rect. */
void SCR_LayoutDrawNameTag(uiFrame_t const *frame, rect_t const *screen) {
    uiNameTag_t const *tag = frame->buffer.data;
    rect_t text = { screen->x + tag->padding_x, screen->y + tag->padding_y,
                  screen->w - tag->padding_x * 2, screen->h - tag->padding_y * 2 };
    drawText_t dt = SCR_GetDrawText(frame, text.w, SCR_GetStringValue(frame), &tag->text);
    SCR_LayoutDrawBackdrop2(frame, screen, &tag->background);
    dt.rect = text; dt.flags |= DRAW_WORD_WRAP; re.DrawText(&dt);
}

void SCR_LayoutDrawTextArea(uiFrame_t const *frame, rect_t const *screen) {
    uiTextArea_t const *ta = frame->buffer.data;
    cstring_t value = SCR_GetStringValue(frame);
    rect_t view = SCR_LayoutTextAreaView(frame, screen);
    drawText_t dt = SCR_GetDrawText(frame, view.w, value ? value : "", &(uiLabel_t){
        .font = ta->font, .textalignx = FONT_JUSTIFYLEFT, .textaligny = FONT_JUSTIFYTOP });
    float full_height = view.h;
    float max_scroll = 0.0f;

    dt.flags |= DRAW_WORD_WRAP;
    if (re.GetTextSize && view.w > 0.0f && view.h > 0.0f) {
        full_height = MAX(view.h, re.GetTextSize(&dt).y);
        max_scroll = MAX(0.0f, full_height - view.h);
    }
    dt.rect = MAKE(rect_t, view.x,
                   view.y - max_scroll * MIN(MAX(frame->value, 0.0f), 1.0f),
                   view.w, full_height);
    dt.flags |= DRAW_CLIP;
    dt.clip = view;
    re.DrawText(&dt);
}

/* Use the same font and inset as SCR_LayoutDrawEditBox for pointer selection. */
uint32_t SCR_LayoutEditCaretAt(uiFrame_t const *frame, cstring_t value, float x) {
    uiEditBox_t const *edit = frame && frame->buffer.size >= sizeof(uiEditBox_t) ? frame->buffer.data : NULL;
    uiFrame_t const *text_frame = NULL;
    uiLabel_t label = { 0 };
    drawText_t dt;
    rect_t inner, rect;
    char prefix[256];
    float previous = 0.0f, target;
    size_t len;
    if (!edit || !value) return 0;
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *child = SCR_Frame(i);
        if (child && child->parent == frame->number &&
            (child->flags.type == FT_STRING || child->flags.type == FT_TEXT)) {
            text_frame = child;
            break;
        }
    }
    if (!text_frame) return 0;
    inner = Rect_inset(SCR_LayoutRect(frame), MAX(edit->borderSize, 0.0f));
    rect = MAKE(rect_t, .x = inner.x, .y = SCR_LayoutRect(text_frame)->y,
                       .w = inner.w, .h = SCR_LayoutRect(text_frame)->h);
    if (text_frame->buffer.data && text_frame->buffer.size >= sizeof(uiLabel_t))
        label = *(uiLabel_t const *)text_frame->buffer.data;
    if (edit->font) label.font = edit->font;
    dt = SCR_GetDrawText(text_frame, rect.w, value, &label);
    dt.rect = rect;
    dt.font = edit->font ? cl.fonts[edit->font] : dt.font;
    dt.halign = FONT_JUSTIFYLEFT;
    target = MAX(0.0f, x - rect.x);
    len = MIN(strlen(value), sizeof(prefix) - 1);
    for (size_t n = 1; n <= len; n++) {
        float width;
        memcpy(prefix, value, n);
        prefix[n] = '\0';
        dt.text = prefix;
        width = re.GetTextSize(&dt).x;
        if (target < (previous + width) * 0.5f) return (uint32_t)(n - 1);
        previous = width;
    }
    return (uint32_t)len;
}

void SCR_LayoutDrawEditBox(uiFrame_t const *frame, rect_t const *screen) {
    uiEditBox_t const *edit = frame->buffer.data;
    uiFrame_t const *text_frame = NULL;
    cstring_t value = NULL;
    uint32_t cursor = 0;
    uiLabel_t label = { 0 };
    drawText_t dt;
    rect_t text_rect;

    if (!edit || frame->buffer.size < sizeof(*edit)) return;
    SCR_LayoutDrawBackdrop2(frame, screen, &edit->background);

    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *child = SCR_Frame(i);
        if (!child || child->parent != frame->number) continue;
        if (child->flags.type == FT_STRING || child->flags.type == FT_TEXT) {
            text_frame = child;
            break;
        }
    }
    if (!text_frame) return;

    value = CL_WindowEditTextValue(text_frame->number);
    if (!value) value = SCR_GetStringValue(text_frame);
    if (!value) value = "";

    if (text_frame->buffer.data && text_frame->buffer.size >= sizeof(uiLabel_t))
        label = *(uiLabel_t const *)text_frame->buffer.data;
    if (edit->font) label.font = edit->font;

    /* The retail edit box text child is frequently authored as a narrow,
     * centred STRING.  That is suitable when the native edit-box renderer
     * owns clipping, but it is not the editable viewport itself.  Use the
     * parent control's inner rectangle horizontally so rendered text, cursor,
     * and clipping match the complete edit-box background.  Preserve the
     * authored child Y/height for vertical placement. */
    rect_t inner = Rect_inset(screen, MAX(edit->borderSize, 0.0f));
    rect_t authored = *SCR_LayoutRect(text_frame);
    text_rect = MAKE(rect_t,
        .x = inner.x,
        .y = authored.y,
        .w = inner.w,
        .h = authored.h > 0.0f ? authored.h : inner.h);

    dt = SCR_GetDrawText(text_frame, text_rect.w, value, &label);
    dt.rect = text_rect;
    dt.font = edit->font ? cl.fonts[edit->font] : dt.font;
    dt.color = edit->textColor.a ? edit->textColor : COLOR32_WHITE;
    dt.halign = FONT_JUSTIFYLEFT;
    dt.flags |= DRAW_CLIP;
    dt.clip = text_rect;

    if (Cvar_Integer("ui_window_debug", 0) >= 2)
        fprintf(stderr,
                "UI_WINDOW_DEBUG edit-draw frame=%u textFrame=%u rect=(%.4f,%.4f %.4fx%.4f) "
                "editRect=(%.4f,%.4f %.4fx%.4f) border=%.4f "
                "font=%u color=(%u,%u,%u,%u) text=\"%s\"\n",
                (unsigned)frame->number, (unsigned)text_frame->number,
                text_rect.x, text_rect.y, text_rect.w, text_rect.h,
                screen->x, screen->y, screen->w, screen->h, edit->borderSize,
                (unsigned)label.font, (unsigned)dt.color.r, (unsigned)dt.color.g,
                (unsigned)dt.color.b, (unsigned)dt.color.a, value);

    /* Draw the selected default save name behind the glyphs. */
    {
        uint32_t start, end;
        size_t len = strlen(value);
        if (CL_WindowEditSelection(text_frame->number, &start, &end) &&
            start < end && end <= len && len < 256) {
            char before[256], selected[256];
            drawText_t measure = dt;
            vec2_t prefix_size, selected_size;
            rect_t highlight = text_rect;
            memcpy(before, value, start); before[start] = 0;
            memcpy(selected, value + start, end - start); selected[end - start] = 0;
            measure.text = before;
            prefix_size = re.GetTextSize(&measure);
            measure.text = selected;
            selected_size = re.GetTextSize(&measure);
            highlight.x += prefix_size.x;
            highlight.w = MIN(selected_size.x, MAX(0.0f, text_rect.w - prefix_size.x));
            if (highlight.w > 0.0f)
                re.DrawImageEx(&MAKE(drawImage_t,
                    .texture = NULL, .shader = SHADER_UI, .alphamode = BLEND_MODE_BLEND,
                    .screen = highlight, .uv = MAKE(rect_t, 0, 0, 1, 1),
                    .color = frame->color.a ? frame->color : MAKE(color32_t, 120, 120, 120, 255),
                    .flags = DRAW_CLIP, .clip = text_rect));
        }
    }
    re.DrawText(&dt);
    if (CL_WindowEditCursor(text_frame->number, &cursor))
        M_DrawTextInputCursor(&re, &dt, value, cursor, COLOR32_WHITE);
}

void SCR_LayoutDrawListBox(uiFrame_t const *frame, rect_t const *screen) {
    uiListBox_t const *lb = frame->buffer.data;
    rect_t list_rect = Rect_inset(screen, lb->border);
    float item_height = lb->itemHeight > 0 ? lb->itemHeight : 0.018f;
    int16_t selectedIndex = lb->selectedIndex;
    uint32_t scrollOffset = 0;
    char items[MAX_LISTBOX_TEXT];

    SCR_LayoutDrawBackdrop2(frame, screen, &lb->background);

    uiFrame_t const *scrollbar = NULL;
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *child = SCR_Frame(i);
        if (child && child->parent == frame->number && child->flags.type == FT_SCROLLBAR) {
            scrollbar = child; break;
        }
    }
    if (scrollbar) {
        float sw = MAX(SCR_LayoutRect(scrollbar)->w, 0.0f);
        if (sw > 0 && sw < list_rect.w) list_rect.w -= sw;
    }
    uint32_t maxScroll = (uint32_t)SCR_LayoutListBoxMaxScroll(frame);
    scrollOffset = maxScroll
        ? (uint32_t)lroundf(MIN(MAX(frame->value, 0.0f), 1.0f) * maxScroll)
        : 0;
    if (scrollbar) ((uiFrame_t *)scrollbar)->value = maxScroll ? scrollOffset / (float)maxScroll : 0.0f;
    if (!frame->text || !*frame->text) return;

    snprintf(items, sizeof(items), "%s", frame->text);
    char *save = NULL;
    char *line = strtok_r(items, "\n", &save);
    int index  = 0;
    while (line && index < (int)scrollOffset) { line = strtok_r(NULL, "\n", &save); index++; }

    /* Warsmash creates floor(viewHeight / itemHeight) row frames.  Keep the
     * renderer on the same whole-row count used by scrolling and hit-testing;
     * drawing a partial extra row makes the visual list disagree with both. */
    uint32_t const visible_rows = (uint32_t)SCR_LayoutListBoxVisibleRows(frame, &list_rect);
    uint32_t drawn_rows = 0;
    float item_y = list_rect.y;
    while (line && drawn_rows < visible_rows) {
        char *hidden = strchr(line, '\t');
        if (hidden) *hidden = '\0';
        rect_t row = { list_rect.x, item_y, list_rect.w, item_height };
        if (index == selectedIndex) {
            rect_t selection = row;
            selection.x += 0.0025f;
            selection.y += 0.0020f;
            selection.w = MAX(0.0f, selection.w - 0.0050f);
            selection.h = MAX(0.0f, selection.h - 0.0040f);
            re.DrawImage(cl.pics[0], &selection, &MAKE(rect_t,0,0,1,1),
                         MAKE(color32_t,32,64,180,128));
        }
        re.DrawText(&MAKE(drawText_t,
            .font       = cl.fonts[lb->text.font],
            .text       = line,
            .color      = frame->color.a ? frame->color : COLOR32_WHITE,
            .halign     = FONT_JUSTIFYLEFT,
            .valign     = FONT_JUSTIFYMIDDLE,
            .icons      = cl.pics,
            .lineHeight = 1.33,
            .textWidth  = row.w,
            /* A list row owns only its row rectangle.  Row-local clipping is
             * stricter than clipping the batch to the entire chooser and also
             * prevents glyph overhang from leaking into neighbouring UI. */
            .flags      = DRAW_CLIP,
            .clip       = row,
            .rect       = row));
        item_y += item_height;
        drawn_rows++;
        line = strtok_r(NULL, "\n", &save);
        index++;
    }
}

/* Same renderer for ordinary mouse hover and focused radial-menu commands.
 * The tooltip's image/font/layout still originate from WC3's authored frame. */
static void SCR_DrawAuthoredTooltip(uiFrame_t const *frame, rect_t const *scrn, cstring_t tooltip) {
    if (!tooltip || !*tooltip || !frame || !frame->buffer.data ||
        frame->buffer.size < sizeof(uiTooltip_t)) return;
    uiTooltip_t const *tt = frame->buffer.data;
    float const PAD = 0.005f;
    rect_t screen = *scrn;
    drawText_t dt = SCR_GetDrawText(frame, screen.w - PAD*2, tooltip, &tt->text);
    dt.flags |= DRAW_WORD_WRAP;
    vec2_t tsz = re.GetTextSize(&dt);
    tsz.y    += PAD * 2;
    screen.y += screen.h - tsz.y;
    screen.h  = tsz.y;
    rect_t text = Rect_inset(&screen, PAD);
    SCR_LayoutDrawBackdrop(frame, &screen);
    dt = SCR_GetDrawText(frame, text.w, tooltip, &tt->text);
    dt.rect = text;
    dt.flags |= DRAW_WORD_WRAP;
    re.DrawText(&dt);
}

void SCR_LayoutDrawTooltip(uiFrame_t const *frame, rect_t const *scrn) {
    if (!active_tooltip || gamepad_radial_open) return;
    /* Several HUD layers may carry the shared tooltip presentation frame.
     * Draw it only in the layer/window that owns the hovered source frame so a
     * passive resource tooltip is not overdrawn again by command-card layers. */
    if (layout_hovered) {
        if (!layout_current_window || layout_current != layout_hovered) return;
    } else if (layout_current_window || layout_current_layer != layout_hovered_layer) {
        return;
    }
    SCR_DrawAuthoredTooltip(frame, scrn, active_tooltip);
}

typedef struct { FRAMETYPE type; void (*func)(uiFrame_t const *, rect_t const *); } drawer_t;

static drawer_t updaters[] = {
    { FT_BUILDQUEUE, SCR_LayoutUpdateBuildQueue },
};

static drawer_t drawers[] = {
    { FT_TEXTURE,        SCR_LayoutDrawTexture },
    { FT_HIGHLIGHT,      SCR_LayoutDrawHighlight },
    { FT_BACKDROP,       SCR_LayoutDrawBackdrop },
    { FT_SIMPLESTATUSBAR,SCR_LayoutDrawStatusbar },
    { FT_SEGMENTED_STATUSBAR, SCR_LayoutDrawSegmentedStatusbar },
    { FT_LOADING_BAR,    SCR_LayoutDrawLoadingBar },
    { FT_COMMANDBUTTON,  SCR_LayoutDrawCommandButton },
    { FT_STRING,         SCR_LayoutDrawString },
    { FT_NAMETAG,        SCR_LayoutDrawNameTag },
    { FT_TEXT,           SCR_LayoutDrawString },
    { FT_TEXTAREA,       SCR_LayoutDrawTextArea },
    { FT_EDITBOX,        SCR_LayoutDrawEditBox },
    { FT_GLUEEDITBOX,    SCR_LayoutDrawEditBox },
    { FT_SLASHCHATBOX,   SCR_LayoutDrawEditBox },
    { FT_LISTBOX,        SCR_LayoutDrawListBox },
    { FT_SCROLLBAR,      SCR_LayoutDrawScrollBar },
    { FT_SLIDER,         SCR_LayoutDrawScrollBar },
    { FT_TOOLTIPTEXT,    SCR_LayoutDrawTooltip },
    { FT_MODEL,          SCR_LayoutDrawPortrait },
    { FT_SPRITE,         SCR_LayoutDrawSprite },
    { FT_PORTRAIT,       SCR_LayoutDrawPortrait },
    { FT_MINIMAP,        CL_LayoutDrawMinimap },
    { FT_BUILDQUEUE,     SCR_LayoutDrawBuildQueue },
    { FT_MESSAGE_QUEUE,  SCR_LayoutDrawMessageQueue },
    { FT_MULTISELECT,    SCR_LayoutDrawMultiSelect },
    { FT_CHECKBOX,       SCR_LayoutCheckBox },
    { FT_GLUECHECKBOX,   SCR_LayoutCheckBox },
    { FT_SIMPLECHECKBOX, SCR_LayoutCheckBox },
    { FT_SIMPLEBUTTON,   SCR_LayoutSimpleButton },
    { FT_BUTTON,         SCR_LayoutGlueTextButton },
    { FT_TEXTBUTTON,     SCR_LayoutGlueTextButton },
    { FT_POPUPMENU,      SCR_LayoutGlueTextButton },
    { FT_GLUEPOPUPMENU,  SCR_LayoutGlueTextButton },
    { FT_GLUETEXTBUTTON, SCR_LayoutGlueTextButton },
    { FT_GLUEBUTTON,     SCR_LayoutGlueTextButton },
};

void SCR_LayoutDrawFrame(uiFrame_t const *frame) {
    rect_t const *screen = SCR_LayoutRect(frame);
    /* Model sprites and dynamic drawers legitimately have no authored size.
     * Only absent context bindings suppress drawing, just as they collapse layout. */
    if (!SCR_LayoutContextFrameVisible(frame) || !SCR_LayoutFrameRowsVisible(frame)) return;
    FOR_LOOP(j, sizeof(drawers)/sizeof(*drawers)) {
        if (drawers[j].type == frame->flags.type) {
            drawers[j].func(frame, screen);
            break;
        }
    }
}

void SCR_LayoutUpdateFrame(uiFrame_t const *frame) {
    rect_t const *screen = SCR_LayoutRect(frame);
    if (!SCR_LayoutFrameRowsVisible(frame)) return;

    /* Tooltip ownership is a generic frame contract, not a command-button
     * behavior. Resource-bar labels/icons and other passive HUD frames may
     * carry tooltip text without becoming clickable controls. */
    if (SCR_LayoutFrameIsHovered(frame) && frame->tooltip && *frame->tooltip)
        active_tooltip = SCR_GetTooltipText(frame);

    FOR_LOOP(j, sizeof(updaters)/sizeof(*updaters)) {
        if (updaters[j].type == frame->flags.type) {
            updaters[j].func(frame, screen);
            break;
        }
    }
}

static void SCR_GamepadRadialDraw(void);
static void SCR_GamepadDrawOverlay(void) {
    SCR_GamepadRadialDraw();
    if (local_notification[0]) {
        uint32_t const now = SDL_GetTicks();
        if ((int32_t)(now - local_notification_end_ms) >= 0) {
            local_notification[0] = '\0';
        } else if (re.DrawText) {
            uiCanvas_t const *canvas = CL_Canvas();
            font_t const *font = SCR_LocalNotificationFont();
            if (font && canvas->scene.w > 0.0f && canvas->scene.h > 0.0f) {
                rect_t const rect = {
                    canvas->scene.x + canvas->scene.w * (0.211f / 0.8f),
                    canvas->scene.y + canvas->scene.h * (0.4145f / 0.6f),
                    canvas->scene.w * (0.62f / 0.8f),
                    canvas->scene.h * (0.035f / 0.6f),
                };
                drawText_t text = {
                    .font = font, .text = local_notification, .rect = rect,
                    .color = COLOR32_BLACK, .textWidth = rect.w,
                    .halign = FONT_JUSTIFYLEFT, .valign = FONT_JUSTIFYTOP,
                };
                text.rect.x += 0.001f;
                text.rect.y += 0.001f;
                re.DrawText(&text);
                text.rect = rect;
                text.color = MAKE(color32_t, 255, 204, 0, 255);
                re.DrawText(&text);
            }
        }
    }
    if (gamepad_selection_visible && re.DrawSelectionCircle)
        re.DrawSelectionCircle(gamepad_selection_center, gamepad_selection_radius,
                               MAKE(color32_t, 255, 255, 255, 245));
}

/* Only actionable server-owned frames are eligible. The game's authored
 * 4x3 command grid is mapped to the requested ten-clock layout, while build
 * submenu buttons use ordinary clockwise order and overflow onto RT pages. */
static void SCR_GamepadCaptureFrame(uiFrame_t const *frame) {
    if (!frame) return;
    if (!layout_current_window && layout_current_layer == LAYER_INFOPANEL &&
        frame->flags.type == FT_SIMPLEFRAME && (frame->flagsvalue & UIFLAG_GAMEPAD_QUEUE_ITEM) &&
        gamepad_queue_cell_count < GAMEPAD_QUEUE_MAX) {
        rect_t const *rect = SCR_LayoutRect(frame);
        if (rect && rect->w > 0.001f && rect->h > 0.001f &&
            gamepad_queue_cell_count < GAMEPAD_QUEUE_MAX) {
            gamepad_queue_cells[gamepad_queue_cell_count++] =
                (vec2_t){ rect->x + rect->w * 0.5f, rect->y + rect->h * 0.5f };
        }
    }
    if ((frame->flagsvalue & UIFLAG_GAMEPAD_STATUS_ICON) &&
        frame->flags.type == FT_COMMANDBUTTON && gamepad_hero_cell_count < MAX_LAYOUT_OBJECTS) {
        rect_t const *rect = SCR_LayoutRect(frame);
        if (rect) {
            uint32_t index = gamepad_hero_cell_count++;
            gamepad_hero_cells[index] = (typeof(*gamepad_hero_cells)) {
                .center = { rect->x + rect->w * 0.5f, rect->y + rect->h * 0.5f },
                .row = index,
            };
        }
    }
    if (layout_current_layer == LAYER_INFOPANEL && frame->flags.type == FT_MULTISELECT &&
        frame->buffer.data && frame->buffer.size >= sizeof(uiMultiselect_t)) {
        uiMultiselect_t const *multi = frame->buffer.data;
        rect_t const *rect = SCR_LayoutRect(frame);
        uint32_t available = (frame->buffer.size - sizeof(uiMultiselect_t)) /
                             sizeof(uiMultiselectItem_t);
        uint32_t items = MIN((uint32_t)multi->numitems, available);
        if (rect && multi->numcolumns) FOR_LOOP(i, items) {
            uiMultiselectItem_t const *item = &multi->items[i];
            if (!item->entity || gamepad_status_cell_count >= MAX_SELECTED_ENTITIES) break;
            uint32_t index = gamepad_status_cell_count++;
            gamepad_status_cells[index] = (typeof(*gamepad_status_cells)) {
                .center = { rect->x + multi->offset.x * (i % multi->numcolumns) + rect->w * 0.5f,
                            rect->y + multi->offset.y * (i / multi->numcolumns) + rect->h * 0.5f },
                .entity = item->entity,
                .row = i / multi->numcolumns,
                .column = i % multi->numcolumns,
                .columns = multi->numcolumns,
                .focused = (item->flags & UI_MULTISELECT_ITEM_FOCUSED) != 0,
            };
        }
    }
    if (frame->flags.type == FT_TOOLTIPTEXT && !layout_current_window &&
        layout_current_layer == LAYER_COMMANDBAR && frame->buffer.data &&
        frame->buffer.size >= sizeof(uiTooltip_t) && SCR_LayoutRect(frame)) {
        gamepad_radial_tooltip.frame = *frame;
        memcpy(&gamepad_radial_tooltip.payload, frame->buffer.data, sizeof(uiTooltip_t));
        gamepad_radial_tooltip.frame.buffer.data = &gamepad_radial_tooltip.payload;
        gamepad_radial_tooltip.frame.buffer.size = sizeof(uiTooltip_t);
        gamepad_radial_tooltip.rect = *SCR_LayoutRect(frame);
        gamepad_radial_tooltip.present = true;
    }
    if (!SCR_LayoutFrameHasClickCommand(frame)) return;
    gamepad_radial_diag.clickable++;
    rect_t const *rect = SCR_LayoutRect(frame);
    if (!rect || rect->w <= 0.001f || rect->h <= 0.001f) {
        gamepad_radial_diag.invalid_position++;
        return;
    }
    if (gamepad_nav_count < GAMEPAD_NAV_MAX) {
        gamepad_nav[gamepad_nav_count++] = (typeof(*gamepad_nav)) {
            { rect->x + rect->w * 0.5f, rect->y + rect->h * 0.5f },
            layout_current_window || layout_current_layer == LAYER_QUESTDIALOG ||
                layout_current_layer == LAYER_GAME_RESULT };
    }
    if (frame->flags.type != FT_COMMANDBUTTON) return;
    gamepad_radial_diag.command_buttons++;
    if (!frame->tex.index || frame->tex.index >= MAX_IMAGES || !cl.pics[frame->tex.index]) {
        gamepad_radial_diag.missing_art++;
        return;
    }
    cstring_t command = frame->onclick;
    bool inventory = !strncmp(command, "inventory ", 10);
    bool action = !strncmp(command, "button ", 7) ||
                  !strncmp(command, "research ", 9) || !strncmp(command, "upgrade ", 8);
    if (!inventory && !action) {
        gamepad_radial_diag.unsupported++;
        return;
    }
    int page = 0, sector = 0;
    if (inventory) {
        char *end = NULL;
        long slot = strtol(command + 10, &end, 10);
        static int const item_sectors[6] = {8, 9, 1, 2, 7, 4};
        if (!end || end == command + 10 || *end || slot < 0 || slot >= 6) {
            gamepad_radial_diag.unsupported++;
            return;
        }
        page = 1; sector = item_sectors[slot];
    } else {
        /* SCR_LayoutRect already returns virtual-canvas coordinates. A
         * screen-to-UI conversion here scaled these fractions toward zero,
         * assigning no command button to a radial slot. */
        int col = (int)lroundf((rect->x - 0.6175f) / 0.0434f);
        int row = (int)lroundf((rect->y - 0.4660f) / 0.0440f);
        static int const standard_page[3][4] = { {0,0,0,0}, {1,1,0,0}, {0,0,0,0} };
        static int const standard_sector[3][4] = { {8,9,1,2}, {0,5,5,0}, {7,6,4,3} };
        if (col < 0 || col >= 4 || row < 0 || row >= 3) {
            gamepad_radial_diag.invalid_position++;
            return;
        }
        if (frame->flagsvalue & UIFLAG_GAMEPAD_RADIAL_ORDERED) {
            int ordinal = row * 4 + col;
            page = ordinal / GAMEPAD_RADIAL_SLOTS;
            sector = ordinal % GAMEPAD_RADIAL_SLOTS;
        } else {
            page = standard_page[row][col];
            sector = standard_sector[row][col];
        }
    }
    if (page >= GAMEPAD_RADIAL_PAGES) {
        gamepad_radial_diag.unsupported++;
        return;
    }
    if (gamepad_radial[page][sector].present) {
        gamepad_radial_diag.duplicate++;
        return;
    }
    gamepadRadialItem_t *item = &gamepad_radial[page][sector];
    item->frame = *frame;
    uiCanvas_t const *canvas = CL_Canvas();
    size2_t const window = re.GetWindowSize();
    float const pixels_per_ui_x = canvas->scene.w > 0.0f ? window.width / canvas->scene.w : 0.0f;
    float const pixels_per_ui_y = canvas->scene.h > 0.0f ? window.height / canvas->scene.h : 0.0f;
    item->authored_icon_px = MIN(rect->w * pixels_per_ui_x, rect->h * pixels_per_ui_y);
    entityState_t const *sel = SCR_LayoutSelectedEntity();
    item->active_glow = (frame->flagsvalue & (UIFLAG_ALTERNATE_ACTIVE | UIFLAG_ABILITY_ENGAGED)) != 0 ||
                        (sel && frame->stat != UINT8_MAX && sel->ability == frame->stat);
    strlcpy(item->command, command, sizeof(item->command));
    item->frame.onclick = item->command;
    item->frame.buffer.data = NULL;
    item->frame.buffer.size = 0;
    /* The click command serializer consumes the command-button proxy for its
     * cooldown payload. Capture it into the radial-owned copy. */
    if ((item->frame.flagsvalue & UIFLAG_RADIAL_SHADE) && frame->buffer.data &&
        frame->buffer.size >= sizeof(uiCommandButton_t)) {
        item->frame.buffer.data = &item->command_state;
        memcpy(&item->command_state, frame->buffer.data, sizeof(item->command_state));
        item->frame.buffer.size = sizeof(item->command_state);
    }
    /* Preserve the real command button's cooldown and highlight data. */
    item->present = true;
    gamepad_radial_diag.captured++;
}

void SCR_GamepadRadialRelease(void) {
    if (!gamepad_radial_open) return;
    if (gamepad_radial_sector >= 0 && gamepad_radial_page < GAMEPAD_RADIAL_PAGES) {
        gamepadRadialItem_t *item = &gamepad_radial[gamepad_radial_page][gamepad_radial_sector];
        if (item->present) {
            /* The copied command remains the same one authored by the game,
             * and uses the ordinary command-button execution path. */
            item->frame.onclick = item->command;
            SCR_LayoutSendFrameCommand(&item->frame);
        }
    }
    SCR_GamepadRadialCancel();
}

bool SCR_GamepadRadialToggleAutocast(void) {
    if (!gamepad_radial_open) return false;
    if (gamepad_radial_sector < 0 || gamepad_radial_page >= GAMEPAD_RADIAL_PAGES) return true;
    gamepadRadialItem_t const *item = &gamepad_radial[gamepad_radial_page][gamepad_radial_sector];
    if (item->present && item->frame.flags.type == FT_COMMANDBUTTON && item->frame.text &&
        !strncmp(item->frame.text, "autocast ", 9)) {
        uiFrame_t command_frame = item->frame;
        command_frame.onclick = command_frame.text;
        SCR_LayoutSendFrameCommand(&command_frame);
    }
    return true;
}

/* Fit a complete command-button texture inside each 36-degree sector. The
 * circumference between adjacent icon centres must exceed the icon width;
 * unlike the old 118px ring, this menu is sized from the authored HUD icons. */
static void SCR_GamepadRadialDraw(void) {
    if (!gamepad_radial_open || !re.DrawImageEx) return;
    size2_t const window = re.GetWindowSize();
    uiCanvas_t const *canvas = CL_Canvas();
    int const page = gamepad_radial_page;
    if (window.width <= 0 || window.height <= 0 || page >= GAMEPAD_RADIAL_PAGES) return;

    float const ui_scale_x = canvas->scene.w / (float)window.width;
    float const ui_scale_y = canvas->scene.h / (float)window.height;

    uint32_t const now = SDL_GetTicks();
    bool const report_draws = now - gamepad_radial_diag.last_report_ms >= 1000;
    uint32_t drawn = 0, missing_texture = 0;

    float const scale = MIN(window.width / 1280.0f, window.height / 800.0f);
    float icon = 56.0f * scale;
    FOR_LOOP(slot, GAMEPAD_RADIAL_SLOTS) {
        gamepadRadialItem_t const *entry = &gamepad_radial[page][slot];
        if (entry->present) icon = MAX(icon, entry->authored_icon_px);
    }
    icon = MAX(22.0f, MIN(icon, MIN(window.width * 0.11f, window.height * 0.13f)));
    //icon *= 0.9f;
    float const padding = MAX(4.0f, 8.0f * scale) * 0.6f;
    float radius = 2.76f * icon;
    radius = MIN(radius, MIN(window.width * 0.5f - padding * 2.0f,
                             window.height * 0.5f - padding * 2.0f));
    if (radius <= 0) return;
    /* Keep authored icon dimensions and fit the radial ring within the screen. */
    /* Put icon centers at the outer edge of their sectors. The square's outer
     * edge aligns with the circle, so sectors remain separated tangentially. */
    float const orbit = radius - icon * 0.5f;
    vec2_t const center = { window.width - radius - padding,
                            window.height - radius - padding };
    /* Separate translucent black wedges replace the command-card square and
     * leave the scene visible through both the sectors and their gaps. */
    if (re.DrawRadialSector) {
        color32_t const shade = MAKE(color32_t, 0, 0, 0, 135);
        FOR_LOOP(slot, GAMEPAD_RADIAL_SLOTS)
            re.DrawRadialSector(center, radius, slot, shade);
    }
    if (gamepad_radial_sector >= 0 && re.DrawRadialSector) {
        uint32_t const rgb = WC3_PlayerColorRGB(cl.playerstate.color);
        color32_t const team = MAKE(color32_t, (rgb >> 16) & 0xff, (rgb >> 8) & 0xff,
                                    rgb & 0xff, 145);
        re.DrawRadialSector(center, radius, gamepad_radial_sector, team);
    }
    if (re.DrawSelectionCircle)
        re.DrawSelectionCircle(center, radius, MAKE(color32_t, 255, 235, 155, 210));
    if (re.DrawRadialSpokes)
        re.DrawRadialSpokes(center, radius, MAKE(color32_t, 255, 235, 155, 125));

    FOR_LOOP(slot, GAMEPAD_RADIAL_SLOTS) {
        float const angle = (float)slot * (2.0f * (float)M_PI / GAMEPAD_RADIAL_SLOTS);
        vec2_t const p = { center.x + orbit * sinf(angle), center.y - orbit * cosf(angle) };
        rect_t const image = { p.x - icon * 0.5f, p.y - icon * 0.5f, icon, icon };
        gamepadRadialItem_t const *entry = &gamepad_radial[page][slot];
        if (!entry->present) continue;
        if (entry->frame.tex.index >= MAX_IMAGES || !cl.pics[entry->frame.tex.index]) {
            missing_texture++;
            continue;
        }
        /* Match the command card's real texture coordinates, tint, active glow,
         * and cooldown shader, instead of drawing a generic full-UV picture. */
        rect_t const authored_uv = get_uvrect(entry->frame.tex.coord);
        rect_t const uv = Rect_div(&authored_uv, 0xff);
        rect_t const ui_image = {
            canvas->scene.x + image.x * ui_scale_x,
            canvas->scene.y + image.y * ui_scale_y,
            image.w * ui_scale_x,
            image.h * ui_scale_y,
        };
        re.DrawImageEx(&MAKE(drawImage_t,
            .texture = cl.pics[entry->frame.tex.index],
            .screen = ui_image,
            .uv = uv,
            .color = SCR_CommandButtonColor(&entry->frame),
            .shader = SHADER_COMMANDBUTTON,
            .uActiveGlow = entry->active_glow,
            .uRadialShade = SCR_CommandButtonRadialShade(&entry->frame)));
        if (entry->sparkle_present) {
            rect_t sparkle_screen = ui_image;
            /* The authored sprite is anchored to the command button's top
             * edge; the radial icon needs the same anchor one icon lower. */
            sparkle_screen.y += ui_image.h;
            SCR_LayoutDrawSpriteWithScope(&entry->autocast_sparkle, &sparkle_screen,
                (void const *)(uintptr_t)(entry->sparkle_layer + 1));
        }
        drawn++;
        if (report_draws)
            fprintf(stderr, "[GAMEPAD_RADIAL] draw page=%u sector=%u frame=%u image=%u texture=%p pixel_rect=%.1f,%.1f,%.1f,%.1f ui_rect=%.4f,%.4f,%.4f,%.4f uv=%.3f,%.3f,%.3f,%.3f glow=%d command=\"%s\"\n",
                    page + 1, slot, entry->frame.number, entry->frame.tex.index,
                    (void *)cl.pics[entry->frame.tex.index], image.x, image.y, image.w, image.h,
                    ui_image.x, ui_image.y, ui_image.w, ui_image.h,
                    uv.x, uv.y, uv.w, uv.h, entry->active_glow, entry->command);
    }
    if (report_draws) {
        gamepad_radial_diag.last_report_ms = now;
        fprintf(stderr, "[GAMEPAD_RADIAL] page=%d sector=%d clickable=%u buttons=%u captured=%u drawn=%u missing_art=%u missing_texture=%u unsupported=%u invalid_position=%u duplicate=%u\n",
                page + 1, gamepad_radial_sector, gamepad_radial_diag.clickable,
                gamepad_radial_diag.command_buttons, gamepad_radial_diag.captured, drawn,
                gamepad_radial_diag.missing_art, missing_texture,
                gamepad_radial_diag.unsupported, gamepad_radial_diag.invalid_position,
                gamepad_radial_diag.duplicate);
    }
    if (re.DrawCharScaled) {
        float const page_scale = 2.0f * scale;
        int const digit = '1' + page;
        re.DrawCharScaled(center.x - 4.0f * page_scale,
                          center.y - 4.0f * page_scale, digit, page_scale);
    }

    /* Left-stick sector focus is equivalent to hovering the underlying WC3
     * command button. Resolve its real authored tooltip (including dynamic
     * substitutions), and paint it with the same tooltip frame/font/backdrop
     * as mouse hovering. Keep it outside the black command surface. */
    if (gamepad_radial_sector >= 0 && gamepad_radial_sector < GAMEPAD_RADIAL_SLOTS &&
        gamepad_radial_tooltip.present) {
        gamepadRadialItem_t const *entry = &gamepad_radial[page][gamepad_radial_sector];
        cstring_t tooltip = entry->present ? SCR_GetTooltipText(&entry->frame) : NULL;
        if (tooltip && *tooltip) {
            rect_t tip = gamepad_radial_tooltip.rect;
            float const left = canvas->scene.x + (center.x - radius - padding) * ui_scale_x;
            float const gap = 12.0f * ui_scale_x;
            float const margin_x = 8.0f * ui_scale_x;
            float const margin_y = 8.0f * ui_scale_y;
            tip.w = MIN(tip.w, MAX(0.08f, left - canvas->scene.x - gap - margin_x));
            tip.x = MAX(canvas->scene.x + margin_x, left - gap - tip.w);
            tip.y = canvas->scene.y + center.y * ui_scale_y - tip.h * 0.5f;
            tip.y = MAX(canvas->scene.y + margin_y,
                        MIN(canvas->scene.y + canvas->scene.h - tip.h - margin_y, tip.y));
            SCR_DrawAuthoredTooltip(&gamepad_radial_tooltip.frame, &tip, tooltip);
        }
    }
}

static void SCR_LayoutRunFrames(handle_t layout, void (*fn)(uiFrame_t const *)) {
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *frame = SCR_Frame(i);
        if (frame) fn(frame);
    }
}

void SCR_LayoutUpdateTooltip(handle_t layout) {
    SCR_LayoutRunFrames(layout, SCR_LayoutUpdateFrame);
}

void SCR_LayoutDrawOverlay(handle_t layout) {
    SCR_BeginSelectedEntityDraw();
    layout_current = layout;
    if (!layout_current_window && layout_current_layer == LAYER_LOADING)
        layout_current_layer = LAYER_INFOPANEL;
    if (layout_current_window) {
        gamepad_nav_count = 0;
    }
    FOR_LOOP(i, SCR_NumFrames()) SCR_GamepadCaptureFrame(SCR_Frame(i));
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *frame = SCR_Frame(i);
        if (!frame || frame->flags.type != FT_SPRITE || !(frame->flagsvalue & UIFLAG_SPRITE_OVERLAY))
            continue;
        FOR_LOOP(page, GAMEPAD_RADIAL_PAGES) FOR_LOOP(slot, GAMEPAD_RADIAL_SLOTS) {
            gamepadRadialItem_t *item = &gamepad_radial[page][slot];
            if (!item->present || item->frame.number != frame->parent) continue;
            item->autocast_sparkle = *frame;
            item->sparkle_layer = layout_current_layer;
            item->sparkle_present = true;
        }
    }
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *f = SCR_Frame(i);
        if (f && f->flags.type == FT_SPRITE && !(f->flagsvalue & UIFLAG_SPRITE_OVERLAY)) SCR_LayoutDrawFrame(f);
    }
    /* Paint the common sibling stack as texture background -> status bar ->
     * remaining frames/text. Frame numbers can reflect FDF parse order rather
     * than paint order: the Hero level label belongs above its XP bar, while
     * hover-bar fills belong above their FT_TEXTURE backings. */
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *f = SCR_Frame(i);
        if (f && f->flags.type == FT_TEXTURE && !(f->flagsvalue & UIFLAG_TEXTURE_OVERLAY))
            SCR_LayoutDrawFrame(f);
    }
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *f = SCR_Frame(i);
        if (f && f->flags.type == FT_SIMPLESTATUSBAR) SCR_LayoutDrawFrame(f);
    }
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *f = SCR_Frame(i);
        if (f && f->flags.type != FT_SPRITE && f->flags.type != FT_TEXTURE &&
            f->flags.type != FT_SIMPLESTATUSBAR &&
            !(f->flags.type == FT_TEXT && (f->flagsvalue & UIFLAG_TEXT_OVERLAY)))
            SCR_LayoutDrawFrame(f);
    }
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *f = SCR_Frame(i);
        if (f && (f->flags.type == FT_GLUETEXTBUTTON || f->flags.type == FT_GLUEBUTTON))
            SCR_LayoutDrawGlueTextButtonHighlight(f);
    }
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *f = SCR_Frame(i);
        if (f && f->flags.type == FT_TEXTURE && (f->flagsvalue & UIFLAG_TEXTURE_OVERLAY))
            SCR_LayoutDrawFrame(f);
    }
    FOR_LOOP(i, SCR_NumFrames()) {
        uiFrame_t const *f = SCR_Frame(i);
        if (f && f->flags.type == FT_SPRITE && (f->flagsvalue & UIFLAG_SPRITE_OVERLAY)) SCR_LayoutDrawFrame(f);
    }
    /* Paint captions after the sprite, rightmost first. The rank belongs to
     * high flag bits, never frame.stat (which changes the label's text). */
    for (int rank = 3; rank >= 0; --rank) {
        FOR_LOOP(i, SCR_NumFrames()) {
            uiFrame_t const *f = SCR_Frame(i);
            if (f && f->flags.type == FT_TEXT && (f->flagsvalue & UIFLAG_TEXT_OVERLAY) &&
                (int)((f->flagsvalue & UIFLAG_TEXT_RANK_MASK) >> UIFLAG_TEXT_RANK_SHIFT) == rank)
                SCR_LayoutDrawFrame(f);
        }
    }
    SCR_EndSelectedEntityDraw();
}

/* The server marks the focused subgroup in the multiselect HUD payload. Reuse
 * that authored identity for the gamepad chord instead of duplicating WC3's
 * subgroup rules in the universal client. */
uint32_t SCR_GamepadFocusedSubgroup(uint32_t *entities, uint32_t capacity) {
    uint32_t count = 0;
    if (!entities || !capacity) return 0;
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        handle_t layout = layout_layers[layer];
        if (!layout || layer == LAYER_WORLD_HOVER ||
            ((1u << layer) & cl.playerstate.uiflags)) continue;
        SCR_Clear(layout);
        FOR_LOOP(i, SCR_NumFrames()) {
            uiFrame_t const *frame = SCR_Frame(i);
            if (!frame || frame->flags.type != FT_MULTISELECT || !frame->buffer.data ||
                frame->buffer.size < sizeof(uiMultiselect_t)) continue;
            uiMultiselect_t const *multi = frame->buffer.data;
            uint32_t available = (frame->buffer.size - sizeof(uiMultiselect_t)) /
                                 sizeof(uiMultiselectItem_t);
            uint32_t items = MIN((uint32_t)multi->numitems, available);
            FOR_LOOP(j, items) {
                uiMultiselectItem_t const *item = &multi->items[j];
                if (!(item->flags & UI_MULTISELECT_ITEM_FOCUSED) || !item->entity) continue;
                bool duplicate = false;
                FOR_LOOP(k, count) if (entities[k] == item->entity) { duplicate = true; break; }
                if (!duplicate && count < capacity) entities[count++] = item->entity;
            }
        }
    }
    return count;
}
void SCR_DrawLayout(void) {
    active_tooltip = NULL;
    SCR_BeginSelectedEntityDraw();

    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        uint32_t flags = cl.playerstate.uiflags;
        if (layer == LAYER_LOADING) continue;
        if ((1 << layer) & flags) continue;
        handle_t layout = layout_layers[layer];
        if (layout) {
            rect_t root;
            layout_current_window = false;
            layout_current_layer = layer;
            SCR_ClearLayer(layout, layer);
            if (layer == LAYER_WORLD_HOVER) {
                if (!SCR_LayoutWorldHoverRoot(&root)) continue;
                SCR_SetLayoutRoot(&root);
            }
            SCR_LayoutUpdateTooltip(layout);
            SCR_LayoutDrawOverlay(layout);
        }
    }
    /* Transient windows are frontmost gameplay UI. The window manager already
     * preserves server-authored z-order, but previously had no screen caller. */
    CL_WindowDraw();
    SCR_EndSelectedEntityDraw();
}

/* The initial layout is a loading-only packet, not a gameplay HUD layer. */
void SCR_DrawLoadingLayout(void) {
    handle_t layout = layout_layers[LAYER_LOADING];
    rect_t root;

    if (!layout) return;
    layout_current_window = false;
    layout_current_layer = LAYER_LOADING;
    SCR_ClearLayer(layout, LAYER_LOADING);
    root = SCR_LayoutSceneRect();
    SCR_SetLayoutRoot(&root);
    SCR_LayoutDrawOverlay(layout);
}

void SCR_SetLayoutLayer(uint32_t layer, handle_t data) {
    if (layer < MAX_LAYOUT_LAYERS) layout_layers[layer] = data;
}

void SCR_ClearLayoutLayer(uint32_t layer) {
    if (layer < MAX_LAYOUT_LAYERS) layout_layers[layer] = NULL;
}

void SCR_ReleaseLayoutCameraEvents(uint32_t layer) {
    if (layer >= MAX_LAYOUT_LAYERS || !re.ReleaseEntityCameraEvents) return;
    uintptr_t const prefix = ((uintptr_t)1 << (sizeof(uintptr_t) * 8 - 1)) | ((uintptr_t)layer << 10);
    FOR_LOOP(number, MAX_LAYOUT_OBJECTS)
        re.ReleaseEntityCameraEvents(prefix | number);
}

void SCR_ReleaseWindowCameraEvents(uintptr_t window_namespace) {
    if (!window_namespace || !re.ReleaseEntityCameraEvents) return;
    uintptr_t const prefix = ((uintptr_t)1 << (sizeof(uintptr_t) * 8 - 1)) |
                            (window_namespace << 10);
    FOR_LOOP(number, MAX_LAYOUT_OBJECTS)
        re.ReleaseEntityCameraEvents(prefix | number);
}

void SCR_ClearLayoutResources(void) {
    FOR_LOOP(i, MAX_DYNAMIC_IMAGES) {
        SAFE_DELETE(layout_dynamic_pics[i], re.ReleaseTexture);
        layout_dynamic_pic_names[i][0] = '\0';
    }
    layout_dynamic_pic_cursor = 0;
}

static bool SCR_LayoutLayerVisible(uint32_t layer) { return layer < MAX_LAYOUT_LAYERS && layout_layers[layer] && !((1u << layer) & cl.playerstate.uiflags); }

/* Result screens outrank Quest when malformed/server-overlapping modal layers coexist. */
static int SCR_LayoutModalLayer(void) {
    if (SCR_LayoutLayerVisible(LAYER_GAME_RESULT)) return LAYER_GAME_RESULT;
    if (SCR_LayoutLayerVisible(LAYER_QUESTDIALOG)) return LAYER_QUESTDIALOG;
    return -1;
}

bool SCR_LayoutModalActive(void) { return SCR_LayoutModalLayer() >= 0; }

static void SCR_LayoutApplyOrderQueueModifier(uiFrame_t const *frame, char *command, size_t size,
                                              bool shift_held) {
    if (!shift_held || !frame || !(frame->flagsvalue & UIFLAG_ORDER_QUEUEABLE)) return;
    strlcat(command, " queue", size);
}

bool SCR_LayoutMouseEvent(layoutMouseEvent_t const *mouse_event) {
    menuMouseEvent_t const event = mouse_event->event;
    int const x = mouse_event->x;
    int const y = mouse_event->y;
    int32_t const param = mouse_event->param;
    bool const shift_held = mouse_event->shift_held;
    vec2_t const point = SCR_ScreenToUI(x, y);
    uiFrame_t const *hovered_frame = NULL;
    int const modal_layer = SCR_LayoutModalLayer();
    /* This path handles persistent layout layers, not client-managed windows. */
    layout_current_window = false;
    layout_hovered = NULL;
    layout_hovered_number = 0;
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        handle_t layout = layout_layers[layer];
        if (modal_layer >= 0 && (int)layer != modal_layer) continue;
        uint32_t flags = cl.playerstate.uiflags;
        if (!layout || layer == LAYER_WORLD_HOVER || (1 << layer) & flags) continue;
        SCR_Clear(layout);
        for (uint32_t i = SCR_NumFrames(); i > 0; i--) {
            uiFrame_t const *frame = SCR_Frame(i - 1);
            bool hit;

            if (!frame || !SCR_LayoutFrameRowsVisible(frame)) continue;
            if (frame->flags.type == FT_MULTISELECT)
                hit = SCR_LayoutMultiselectEntityAt(frame, &point) != 0;
            else
                hit = (SCR_LayoutFrameHasClickCommand(frame) ||
                       (frame->tooltip && *frame->tooltip)) &&
                      Rect_contains(SCR_LayoutRect(frame), &point);
            if (hit) {
                layout_hovered_number = frame->number;
                hovered_frame = frame;
                layout_hovered_layer = layer;
                break;
            }
        }
        if (layout_hovered_number) break;
    }

    /* Proxy command buttons may carry a secondary command in text. Warcraft
     * uses this for right-click autocast toggles while preserving left-click
     * targeting on the same icon. Consume both right-button edges so the click
     * cannot also become a world Smart order. */
    if (param == SDL_BUTTON_RIGHT) {
        bool const valid = hovered_frame && hovered_frame->flags.type == FT_COMMANDBUTTON &&
                           hovered_frame->text && *hovered_frame->text;
        if (event == MENU_MOUSE_DOWN) {
            layout_right_press_frame = valid ? hovered_frame->number : 0;
            layout_right_press_layer = valid ? layout_hovered_layer : 0;
            return valid;
        }
        if (event != MENU_MOUSE_UP) return false;
        bool const matched = valid && layout_right_press_frame == hovered_frame->number &&
                             layout_right_press_layer == layout_hovered_layer;
        layout_right_press_frame = 0;
        if (!matched) return false;
        MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
        SZ_Printf(&cls.netchan.message, "%s", hovered_frame->text);
        return true;
    }

    if (param != SDL_BUTTON_LEFT) return false;
    if (event == MENU_MOUSE_DOWN) {
        char command[CMDARG_LEN * 2];
        layout_left_down = true;
        layout_press_frame = hovered_frame ? hovered_frame->number : 0;
        layout_press_layer = hovered_frame ? layout_hovered_layer : 0;
        /* Multiselect icons are server-authored gameplay controls. They do not
         * have an onclick string, so consume the press here and resolve the
         * concrete entity on release below. */
        if (hovered_frame && hovered_frame->flags.type == FT_MULTISELECT) return true;
        if (!hovered_frame || layout_held_command[0]) return false;
        SCR_LayoutFormatOnClickCommand(hovered_frame->onclick, command, sizeof(command));
        if (command[0] != '+') return false;
        strlcpy(layout_held_command, command, sizeof(layout_held_command));
        MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
        SZ_Printf(&cls.netchan.message, "%s", layout_held_command);
        return false;
    }
    if (event != MENU_MOUSE_UP) return false;
    layout_left_down = false;
    bool const matched = hovered_frame && layout_press_frame == hovered_frame->number &&
                         layout_press_layer == layout_hovered_layer;
    layout_press_frame = 0;
    if (layout_held_command[0]) {
        char command[sizeof(layout_held_command)];
        strlcpy(command, layout_held_command, sizeof(command));
        command[0] = '-';
        layout_held_command[0] = '\0';
        MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
        SZ_Printf(&cls.netchan.message, "%s", command);
        return false;
    }

    if (!matched) return false;
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        handle_t layout = layout_layers[layer];
        uint32_t flags = cl.playerstate.uiflags;
        if (modal_layer >= 0 && (int)layer != modal_layer) continue;
        if (!layout || layer != layout_hovered_layer || layer == LAYER_WORLD_HOVER || (1 << layer) & flags) continue;
        SCR_Clear(layout);
        for (uint32_t i = SCR_NumFrames(); i > 0; i--) {
            uiFrame_t const *frame = SCR_Frame(i - 1);

            if (!frame || frame->number != hovered_frame->number) continue;
            if (frame->flags.type == FT_MULTISELECT) {
                uint32_t entity = SCR_LayoutMultiselectEntityAt(frame, &point);
                if (entity) {
                    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
                    SZ_Printf(&cls.netchan.message, "focus %u", (unsigned)entity);
                    return true;
                }
                continue;
            }
            if (!SCR_LayoutFrameHasClickCommand(frame)) continue;
            if (Rect_contains(SCR_LayoutRect(frame), &point)) {
                char command[CMDARG_LEN * 2];
                SCR_LayoutFormatOnClickCommand(frame->onclick, command, sizeof(command));
                SCR_LayoutApplyOrderQueueModifier(frame, command, sizeof(command), shift_held);
                MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
                SZ_Printf(&cls.netchan.message, "%s", command);
                return false;
            }
        }
    }
    return false;
}

/* Dispatch a command-button hotkey the same way a mouse click on that */
bool SCR_LayoutKeyEvent(int key, bool shift_held) {
    int const upper = toupper(key);
    int const modal_layer = SCR_LayoutModalLayer();

    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        handle_t layout = layout_layers[layer];
        if (modal_layer >= 0 && (int)layer != modal_layer) continue;
        uint32_t flags = cl.playerstate.uiflags;
        if (!layout || layer == LAYER_WORLD_HOVER || (1 << layer) & flags) continue;
        SCR_Clear(layout);
        for (uint32_t i = SCR_NumFrames(); i > 0; i--) {
            uiFrame_t const *frame = SCR_Frame(i - 1);
            if (!frame || !SCR_LayoutFrameHasClickCommand(frame)) continue;
            bool const is_cancel = key == K_ESCAPE && !strcmp(frame->onclick, "button CmdCancel");
            if (is_cancel || (frame->hotkey && toupper(frame->hotkey) == upper)) {
                char command[CMDARG_LEN * 2];
                SCR_LayoutFormatOnClickCommand(frame->onclick, command, sizeof(command));
                SCR_LayoutApplyOrderQueueModifier(frame, command, sizeof(command), shift_held);
                MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
                SZ_Printf(&cls.netchan.message, "%s", command);
                return true;
            }
        }
    }
    return false;
}

static bool SCR_LayoutSelectionBlockerType(FRAMETYPE type) {
    switch (type) {
        case FT_TEXTURE:
        case FT_BACKDROP:
        case FT_SIMPLESTATUSBAR:
        case FT_SEGMENTED_STATUSBAR:
        case FT_COMMANDBUTTON:
        case FT_MODEL:
        case FT_PORTRAIT:
        case FT_MINIMAP:
        case FT_BUILDQUEUE:
        case FT_MESSAGE_QUEUE:
        case FT_MULTISELECT:
        case FT_CHECKBOX:
        case FT_GLUECHECKBOX:
        case FT_SIMPLECHECKBOX:
        case FT_SIMPLEBUTTON:
        case FT_BUTTON:
        case FT_TEXTBUTTON:
        case FT_GLUETEXTBUTTON:
        case FT_GLUEBUTTON:
            return true;
        default:
            return false;
    }
}

static bool SCR_RangesOverlap(float a0, float a1, float b0, float b1) {
    return MAX(a0, b0) < MIN(a1, b1);
}

/* The WC3 command console is not a flat rectangle: the info/status and command
 * panels protrude above the world viewport's bottom edge.  Selection starts in
 * the world, so constrain its moving corner against visible bottom-console
 * frames before the renderer sees the marquee.  This keeps selection geometry
 * and selection hit-testing out of authored HUD regions instead of relying on
 * transparent console art to hide the line.  Full-screen world games have a
 * scissor bottom at UI_BASE_HEIGHT, making this a no-op. */
void SCR_LayoutClampSelectionRect(rect_t *rect) {
    vec2_t start, finish, clamped;
    float world_bottom;
    float xmin, xmax;
    size2_t window;

    if (!rect) {
        return;
    }
    window = re.GetWindowSize();
    if (window.width <= 0 || window.height <= 0 || cl.viewDef.scissor.y <= 0.0f) {
        return;
    }

    start = SCR_ScreenToUI((int)rect->x, (int)rect->y);
    finish = SCR_ScreenToUI((int)(rect->x + rect->w), (int)(rect->y + rect->h));
    clamped = finish;
    world_bottom = (1.0f - cl.viewDef.scissor.y) * UI_BASE_HEIGHT;
    xmin = MIN(start.x, finish.x);
    xmax = MAX(start.x, finish.x);

    /* Clamp vertical travel first.  Only frames that extend into the bottom
     * console count; upper resource/command strips are not part of this mask. */
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        handle_t layout = layout_layers[layer];
        uint32_t flags = cl.playerstate.uiflags;
        if (!layout || layer == LAYER_WORLD_HOVER || ((1 << layer) & flags)) continue;
        SCR_Clear(layout);
        FOR_LOOP(i, SCR_NumFrames()) {
            uiFrame_t const *frame = SCR_Frame(i);
            rect_t const *r;
            float bottom;
            if (!frame || !SCR_LayoutSelectionBlockerType(frame->flags.type)) continue;
            r = SCR_LayoutRect(frame);
            if (!r || r->w <= 0.0f || r->h <= 0.0f) continue;
            bottom = r->y + r->h;
            if (bottom < world_bottom || !SCR_RangesOverlap(xmin, xmax, r->x, r->x + r->w)) continue;

            if (finish.y > start.y && r->y > start.y && r->y < clamped.y) {
                clamped.y = r->y;
            } else if (finish.y < start.y && bottom < start.y && bottom > clamped.y) {
                clamped.y = bottom;
            }
        }
    }

    rect->w = (clamped.x / SCR_UICanvasWidth()) * window.width - rect->x;
    rect->h = (clamped.y / UI_BASE_HEIGHT) * window.height - rect->y;
}

bool SCR_LayoutHitTest(int x, int y) {
    vec2_t const point = SCR_ScreenToUI(x, y);
    if (SCR_LayoutModalActive()) return true;
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        handle_t layout = layout_layers[layer];
        uint32_t flags = cl.playerstate.uiflags;
        if (!layout || layer == LAYER_WORLD_HOVER || (1 << layer) & flags) continue;
        SCR_Clear(layout);
        FOR_LOOP(i, SCR_NumFrames()) {
            uiFrame_t const *frame = SCR_Frame(i);
            if (!frame) continue;
            /* Persistent controls may intentionally sit over the world instead
             * of over a console texture. Any clickable server-authored frame
             * is gameplay UI and must suppress world selection underneath it. */
            if (frame->flags.type == FT_MULTISELECT) {
                if (SCR_LayoutMultiselectEntityAt(frame, &point)) return true;
                continue;
            }
            /* Texture tiles mask console input even without a command or
             * tooltip; making all textures pass through leaks clicks to the world. */
            if (frame->flags.type != FT_TEXTURE &&
                !SCR_LayoutFrameHasClickCommand(frame) &&
                !(frame->tooltip && *frame->tooltip)) continue;
            if (Rect_contains(SCR_LayoutRect(frame), &point)) return true;
        }
    }
    return false;
}

/* Consume the old wire shape to preserve packet alignment; current games author svc_layout. */
void CL_ParseUnitUI(sizeBuf_t *msg) {
    int count = MSG_ReadByte(msg);
    if (count) fprintf(stderr, "CL_ParseUnitUI: obsolete HUD packet; server must author svc_layout\n");
    for (int i = 0; i < count && msg->readcount < msg->cursize; i++) {
        MSG_ReadShort(msg);
        for (int kind = 0; kind < 3; kind++) {
            int items = MSG_ReadByte(msg), strings = kind == 0 ? 4 : kind == 1 ? 3 : 1;
            for (int j = 0; j < items && msg->readcount < msg->cursize; j++) {
                for (int k = 0; k < strings; k++) MSG_ReadString2(msg);
                if (kind == 2) MSG_ReadShort(msg);
                else MSG_ReadByte(msg);
            }
        }
    }
}

#ifdef BZ_TESTS
#include "shared/test.h"

TEST(client_layout, health_fraction_gradient_follows_portrait_colour) {
    color32_t color;
    color = SCR_HealthFractionColor(0.0f);
    T_EQ(color.r, 255); T_EQ(color.g, 0); T_EQ(color.b, 0);
    color = SCR_HealthFractionColor(0.25f);
    T_EQ(color.r, 255); T_EQ(color.g, 128); T_EQ(color.b, 0);
    color = SCR_HealthFractionColor(0.5f);
    T_EQ(color.r, 255); T_EQ(color.g, 255); T_EQ(color.b, 0);
    color = SCR_HealthFractionColor(0.75f);
    T_EQ(color.r, 128); T_EQ(color.g, 255); T_EQ(color.b, 0);
    color = SCR_HealthFractionColor(1.0f);
    T_EQ(color.r, 0); T_EQ(color.g, 255); T_EQ(color.b, 0);
    color = SCR_HealthFractionColor(-1.0f);
    T_EQ(color.r, 255); T_EQ(color.g, 0);
    color = SCR_HealthFractionColor(2.0f);
    T_EQ(color.r, 0); T_EQ(color.g, 255);
}

/* Exercise transport decoding and actual renderer fraction binding together.
 * Deliberately leave the world snapshot missing: these are HUD-only values. */
TEST(client_layout, hero_statusbars_decode_out_of_view_vitals_and_mana_pool_changes) {
    uint32_t const number = 17;
    __typeof__(cl.ui_entity_stats[number]) const saved = cl.ui_entity_stats[number];
    entityState_t const world = cl.ents[number].current;
    uint8_t data[64];
    sizeBuf_t msg;
    uiFrame_t frame = { .flags = { .type = FT_SIMPLESTATUSBAR }, .stat = ENT_HEALTH, .value = 17.0f };
    float value = -1.0f;

    frame.flagsvalue |= UIFLAG_ENTITY_STATUSBAR;
    cl.ents[number].current = (entityState_t){0};
    cl.ui_entity_stats[number].valid = 0;
    T_ASSERT(!SCR_LayoutStatusbarValue(&frame, &value));
    SZ_Init(&msg, data, sizeof(data));
    MSG_WriteByte(&msg, 0); MSG_WriteByte(&msg, 1);
    MSG_WriteShort(&msg, 17); MSG_WriteByte(&msg, 128); MSG_WriteByte(&msg, 64); MSG_WriteByte(&msg, 1);
    MSG_WriteByte(&msg, svc_nop); /* Other server messages can follow in the same datagram. */
    msg.readcount = 0;
    T_ASSERT(CL_ParseUIEntityStats(&msg));
    T_EQ(msg.readcount, msg.cursize - 1);
    T_ASSERT(SCR_LayoutStatusbarValue(&frame, &value));
    T_FEQ(value, BYTE2FLOAT(128), 0.0001f);
    frame.stat = ENT_MANA;
    T_ASSERT(SCR_LayoutStatusbarValue(&frame, &value));
    T_FEQ(value, BYTE2FLOAT(64), 0.0001f);

    /* Mana bar exists in layout, but is hidden until its pool becomes available. */
    SZ_Init(&msg, data, sizeof(data));
    MSG_WriteByte(&msg, 0); MSG_WriteByte(&msg, 1);
    MSG_WriteShort(&msg, 17); MSG_WriteByte(&msg, 0); MSG_WriteByte(&msg, 0); MSG_WriteByte(&msg, 0);
    msg.readcount = 0;
    CL_ParseUIEntityStats(&msg);
    T_ASSERT(!SCR_LayoutStatusbarValue(&frame, &value));
    frame.stat = ENT_HEALTH;
    T_ASSERT(SCR_LayoutStatusbarValue(&frame, &value));
    T_FEQ(value, 0.0f, 0.0001f);

    SZ_Init(&msg, data, sizeof(data));
    MSG_WriteByte(&msg, 0); MSG_WriteByte(&msg, 1);
    MSG_WriteShort(&msg, 17); MSG_WriteByte(&msg, 100); MSG_WriteByte(&msg, 0); MSG_WriteByte(&msg, 1);
    msg.readcount = 0;
    CL_ParseUIEntityStats(&msg);
    frame.stat = ENT_MANA;
    T_ASSERT(SCR_LayoutStatusbarValue(&frame, &value));
    T_FEQ(value, 0.0f, 0.0001f);
    frame.value = 17.5f;
    T_ASSERT(!SCR_LayoutStatusbarValue(&frame, &value));
    frame.value = (float)MAX_CLIENT_ENTITIES;
    T_ASSERT(!SCR_LayoutStatusbarValue(&frame, &value));
    frame.value = 17.0f;
    frame.stat = ENT_CARGO;
    T_ASSERT(!SCR_LayoutStatusbarValue(&frame, &value));
    cl.ui_entity_stats[number] = saved;
    cl.ents[number].current = world;
}

/* A new roster/owner change must discard stale values before new updates. */
TEST(client_layout, hero_statusbars_clear_roster_on_reuse_and_ignore_malformed_packets) {
    void *saved = malloc(sizeof(cl.ui_entity_stats));
    uint8_t data[64];
    sizeBuf_t msg;
    uiFrame_t frame = { .flags = { .type = FT_SIMPLESTATUSBAR }, .stat = ENT_HEALTH, .value = 17.0f };
    float value;

    T_NOT_NULL(saved);
    if (!saved) return;
    memcpy(saved, cl.ui_entity_stats, sizeof(cl.ui_entity_stats));
    frame.flagsvalue |= UIFLAG_ENTITY_STATUSBAR;
    cl.ui_entity_stats[17].hp = 255;
    cl.ui_entity_stats[17].valid = 1;
    SZ_Init(&msg, data, sizeof(data));
    MSG_WriteByte(&msg, 1); MSG_WriteByte(&msg, 0);
    msg.readcount = 0;
    CL_ParseUIEntityStats(&msg);
    T_ASSERT(!SCR_LayoutStatusbarValue(&frame, &value));

    /* Truncation must not mutate cached values, even with clear=true. */
    cl.ui_entity_stats[17].hp = 128;
    cl.ui_entity_stats[17].valid = 1;
    SZ_Init(&msg, data, sizeof(data));
    MSG_WriteByte(&msg, 1); MSG_WriteByte(&msg, 1); MSG_WriteShort(&msg, 17);
    msg.readcount = 0;
    CL_ParseUIEntityStats(&msg);
    T_EQ(msg.readcount, 0); /* A failed parser leaves cursor ownership to dispatch. */
    T_ASSERT(SCR_LayoutStatusbarValue(&frame, &value));
    T_FEQ(value, BYTE2FLOAT(128), 0.0001f);

    /* A complete packet with an invalid row must also be rejected atomically,
     * including its clear bit. */
    SZ_Init(&msg, data, sizeof(data));
    MSG_WriteByte(&msg, 1); MSG_WriteByte(&msg, 1);
    MSG_WriteShort(&msg, 0); MSG_WriteByte(&msg, 0); MSG_WriteByte(&msg, 0); MSG_WriteByte(&msg, 0);
    msg.readcount = 0;
    CL_ParseUIEntityStats(&msg);
    T_EQ(msg.readcount, 0);
    T_ASSERT(SCR_LayoutStatusbarValue(&frame, &value));
    T_FEQ(value, BYTE2FLOAT(128), 0.0001f);
    memcpy(cl.ui_entity_stats, saved, sizeof(cl.ui_entity_stats));
    free(saved);
}

/* Change only the fields used by selection resolution, so these tests do not
 * disrupt unrelated renderer and client fixtures. Every test restores them. */
TEST(client_layout, selected_entity_lookup_is_reused_only_inside_a_draw) {
    uint32_t const saved_count = cl.num_entities;
    uint32_t saved_flags[4];
    __typeof__(layout_selected_cache) const saved_cache = layout_selected_cache;

    FOR_LOOP(i, 4) {
        saved_flags[i] = cl.ents[i].current.renderfx;
        cl.ents[i].current.renderfx = 0;
    }
    cl.num_entities = 4;
    layout_selected_cache = (__typeof__(layout_selected_cache)){0};
    cl.ents[1].current.renderfx = RF_SELECTED;
    cl.ents[2].current.renderfx = RF_SELECTED;

    /* The old scan's lowest selected slot has precedence. */
    T_EQ(SCR_LayoutSelectedEntity(), &cl.ents[1].current);
    SCR_BeginSelectedEntityDraw();
    T_EQ(SCR_LayoutSelectedEntity(), &cl.ents[1].current);
    cl.ents[1].current.renderfx = 0;
    T_EQ(SCR_LayoutSelectedEntity(), &cl.ents[1].current); /* no second scan */

    /* Nested overlays must not invalidate the enclosing HUD's result. */
    SCR_BeginSelectedEntityDraw();
    T_EQ(SCR_LayoutSelectedEntity(), &cl.ents[1].current);
    SCR_EndSelectedEntityDraw();
    T_EQ(SCR_LayoutSelectedEntity(), &cl.ents[1].current);
    SCR_EndSelectedEntityDraw();

    /* Independent direct draws and following frames see the new selection. */
    T_EQ(SCR_LayoutSelectedEntity(), &cl.ents[2].current);
    SCR_BeginSelectedEntityDraw();
    T_EQ(SCR_LayoutSelectedEntity(), &cl.ents[2].current);
    SCR_EndSelectedEntityDraw();

    FOR_LOOP(i, 4) cl.ents[i].current.renderfx = saved_flags[i];
    cl.num_entities = saved_count;
    layout_selected_cache = saved_cache;
}

TEST(client_layout, selected_entity_lookup_caches_empty_selection_and_restarts) {
    uint32_t const saved_count = cl.num_entities;
    uint32_t saved_flags[3];
    __typeof__(layout_selected_cache) const saved_cache = layout_selected_cache;

    FOR_LOOP(i, 3) {
        saved_flags[i] = cl.ents[i].current.renderfx;
        cl.ents[i].current.renderfx = 0;
    }
    cl.num_entities = 3;
    layout_selected_cache = (__typeof__(layout_selected_cache)){0};

    SCR_BeginSelectedEntityDraw();
    T_NULL(SCR_LayoutSelectedEntity());
    cl.ents[0].current.renderfx = RF_SELECTED;
    T_NULL(SCR_LayoutSelectedEntity()); /* cached NULL, not a new scan */
    SCR_EndSelectedEntityDraw();

    T_EQ(SCR_LayoutSelectedEntity(), &cl.ents[0].current);
    SCR_BeginSelectedEntityDraw();
    T_EQ(SCR_LayoutSelectedEntity(), &cl.ents[0].current);
    SCR_EndSelectedEntityDraw();

    /* A reused slot and a new entity count cannot inherit the old result. */
    cl.num_entities = 0;
    T_NULL(SCR_LayoutSelectedEntity());
    SCR_BeginSelectedEntityDraw();
    T_NULL(SCR_LayoutSelectedEntity());
    SCR_EndSelectedEntityDraw();
    cl.num_entities = 3;
    cl.ents[0].current.renderfx = 0;
    cl.ents[2].current.renderfx = RF_SELECTED;
    SCR_BeginSelectedEntityDraw();
    T_EQ(SCR_LayoutSelectedEntity(), &cl.ents[2].current);
    SCR_EndSelectedEntityDraw();

    FOR_LOOP(i, 3) cl.ents[i].current.renderfx = saved_flags[i];
    cl.num_entities = saved_count;
    layout_selected_cache = saved_cache;
}
#endif
