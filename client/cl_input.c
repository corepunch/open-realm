#include "cl_input_local.h"
#include "cl_control_groups.h"
#include "ui_layout.h"

#include <stdlib.h>
#include <strings.h>

mouseEvent_t mouse;
static keyCode_t mouse_button_keys[8];

/* SDL2 function/arrow keys are 0x40000000+ and don't fit in keyCode_t. */
static keyCode_t CL_SDLKeyToKeyCode(int sym) {
    static struct { int sym; keyCode_t key; } const extra[] = {
        { SDLK_UP, K_UPARROW },
        { SDLK_DOWN, K_DOWNARROW },
        { SDLK_LEFT, K_LEFTARROW },
        { SDLK_RIGHT, K_RIGHTARROW },
    };
    if (sym >= SDLK_F1 && sym <= SDLK_F12)
        return (keyCode_t)(K_F1 + (sym - SDLK_F1));
    FOR_LOOP(i, 4)
        if (extra[i].sym == sym) return extra[i].key;
    return (keyCode_t)sym;
}

static DWORD CL_BindMods(SDL_Keymod m) {
    DWORD mods = 0;
    if (m & KMOD_CTRL) mods |= KEY_MOD_CTRL;
    if (m & KMOD_ALT) mods |= KEY_MOD_ALT;
    if (m & KMOD_SHIFT) mods |= KEY_MOD_SHIFT;
    return mods;
}

static keyCode_t CL_MouseButtonKey(SDL_MouseButtonEvent const *button) {
    if (!button) return 0;
    switch (button->button) {
        case SDL_BUTTON_LEFT: return K_MOUSE1;
        case SDL_BUTTON_RIGHT: return K_MOUSE2;
        case SDL_BUTTON_MIDDLE: return K_MOUSE3;
        default: return 0;
    }
}

BOOL CL_MouseOverGameplayUI(void) {
    return SCR_LayoutHitTest((int)mouse.origin.x, (int)mouse.origin.y);
}

BOOL CL_GameplayInputReady(void) {
    if (cls.key_dest != key_game || cls.state != ca_active ||
        cl.playerstate.client_ui_state != CLIENT_UI_GAME) {
        return false;
    }
#ifdef WC3
    if (SCR_LayoutModalActive()) {
        return false;
    }
#endif
    return true;
}

void CL_Input(void) {
    SDL_Event event;

    mouse.event = UI_EVENT_NONE;
    mouse.wheel = 0;
    while(SDL_PollEvent(&event)) {
        switch(event.type) {
            case SDL_MOUSEBUTTONDOWN:
                {
                    keyCode_t mousevt = CL_MouseButtonKey(&event.button);
                    mouse.origin.x = event.button.x;
                    mouse.origin.y = event.button.y;
                    if (cls.key_dest == key_game) {
                        CL_InputModeMouseButton(&event.button, true);
                    }
                    if (mousevt && cls.key_dest != key_console) {
                        mouse_button_keys[event.button.button] = mousevt;
                        Key_Event(mousevt, CL_BindMods(SDL_GetModState()), true, event.button.timestamp);
                    }
                }
                break;
            case SDL_MOUSEBUTTONUP:
                {
                    keyCode_t mousevt = event.button.button < sizeof(mouse_button_keys)
                                      ? mouse_button_keys[event.button.button]
                                      : 0;
                    mouse.origin.x = event.button.x;
                    mouse.origin.y = event.button.y;
                    if (cls.key_dest == key_game) {
                        CL_InputModeMouseButton(&event.button, false);
                    }
                    if (mousevt && cls.key_dest != key_console) {
                        Key_Event(mousevt, CL_BindMods(SDL_GetModState()), false, event.button.timestamp);
                        mouse_button_keys[event.button.button] = 0;
                    }
                }
                break;
            case SDL_MOUSEMOTION:
                mouse.origin.x = event.motion.x;
                mouse.origin.y = event.motion.y;
                break;
            case SDL_MOUSEWHEEL:
                {
                    int x;
                    int y;

                    SDL_GetMouseState(&x, &y);
                    mouse.origin.x = x;
                    mouse.origin.y = y;
                    mouse.wheel += event.wheel.y;
                }
                break;
        }
        
        switch(event.type) {
            case SDL_TEXTINPUT:
                if (cls.key_dest == key_console) {
                    CON_TextInput(event.text.text);
                } else if (cls.key_dest == key_menu) {
                    menu.TextInput(event.text.text);
                }
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_BACKQUOTE) {
                    CON_ToggleConsole();
                    break;
                }
                if (cls.key_dest == key_console) {
                    CON_KeyEvent(event.key.keysym.sym, true);
                    break;
                }
                /* SDL key-repeat is not a deliberate second press; skip it for
                 * gameplay so held number binds cannot double-tap a control group. */
                if (cls.key_dest == key_game && event.key.repeat)
                    break;
                if (cls.key_dest == key_game && CL_MinimapKeyEvent(event.key.keysym.sym, event.key.repeat != 0)) {
                    break;
                }
                Key_Event(CL_SDLKeyToKeyCode(event.key.keysym.sym), CL_BindMods(event.key.keysym.mod), true, event.key.timestamp);
                break;
            case SDL_KEYUP:
                if (cls.key_dest == key_console || event.key.keysym.sym == SDLK_BACKQUOTE) {
                    CON_KeyEvent(event.key.keysym.sym, false);
                    break;
                }
                Key_Event(CL_SDLKeyToKeyCode(event.key.keysym.sym), CL_BindMods(event.key.keysym.mod), false, event.key.timestamp);
                break;
            case SDL_MOUSEBUTTONDOWN:
                mouse.origin.x = event.button.x;
                mouse.origin.y = event.button.y;
                mouse.button = event.button.button;
                if (cls.key_dest == key_menu && menu.MouseEvent(MENU_MOUSE_DOWN, event.button.x, event.button.y, event.button.button)) {
                    break;
                }
                if (CL_WindowMouseEvent(MENU_MOUSE_DOWN, event.button.x, event.button.y, event.button.button)) break;
                if (SCR_LayoutMouseEvent(MENU_MOUSE_DOWN, event.button.x, event.button.y, event.button.button)) break;
                if (cls.key_dest == key_menu) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        mouse.event = UI_LEFT_MOUSE_DOWN;
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        mouse.event = UI_RIGHT_MOUSE_DOWN;
                    }
                    break;
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mouse.event = UI_LEFT_MOUSE_DOWN;
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    mouse.event = UI_RIGHT_MOUSE_DOWN;
                }
                break;
            case SDL_MOUSEBUTTONUP:
                mouse.origin.x = event.button.x;
                mouse.origin.y = event.button.y;
                mouse.button = 0;
                if (cls.key_dest == key_menu && menu.MouseEvent(MENU_MOUSE_UP, event.button.x, event.button.y, event.button.button)) {
                    break;
                }
                if (CL_WindowMouseEvent(MENU_MOUSE_UP, event.button.x, event.button.y, event.button.button)) break;
                if (SCR_LayoutMouseEvent(MENU_MOUSE_UP, event.button.x, event.button.y, event.button.button)) break;
                if (cls.key_dest == key_menu) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        mouse.event = UI_LEFT_MOUSE_UP;
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        mouse.event = UI_RIGHT_MOUSE_UP;
                    }
                    break;
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mouse.event = UI_LEFT_MOUSE_UP;
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    mouse.event = UI_RIGHT_MOUSE_UP;
                }
                break;
            case SDL_MOUSEMOTION:
                mouse.origin.x = event.motion.x;
                mouse.origin.y = event.motion.y;
                if (cls.key_dest == key_menu)
                    menu.MouseEvent(MENU_MOUSE_MOVE, event.motion.x, event.motion.y, 0);
                if (CL_WindowMouseEvent(MENU_MOUSE_MOVE, event.motion.x, event.motion.y, 0)) break;
                SCR_LayoutMouseEvent(MENU_MOUSE_MOVE, event.motion.x, event.motion.y, 0);
                if (cls.key_dest == key_menu) {
                    break;
                }
                CL_InputModeMouseMotion(&event.motion);
                break;
            case SDL_MOUSEWHEEL:
                {
                    int x, y, n;
                    keyCode_t wheelkey;
                    SDL_GetMouseState(&x, &y);
                    if (cls.key_dest == key_menu)
                        menu.MouseEvent(MENU_MOUSE_SCROLL, x, y, MENU_MOUSE_PARAM(event.wheel.x, event.wheel.y));
                    if (CL_WindowMouseEvent(MENU_MOUSE_SCROLL, x, y, MENU_MOUSE_PARAM(event.wheel.x, event.wheel.y))) break;
                    SCR_LayoutMouseEvent(MENU_MOUSE_SCROLL, x, y, MENU_MOUSE_PARAM(event.wheel.x, event.wheel.y));
                    if (cls.key_dest == key_game && CL_InputModeMouseWheel(&event.wheel))
                        break;
                    /* Discrete wheel ticks are bindable keys (MWHEELUP / MWHEELDOWN). */
                    if (cls.key_dest == key_console || event.wheel.y == 0)
                        break;
                    wheelkey = event.wheel.y > 0 ? K_MWHEELUP : K_MWHEELDOWN;
                    n = event.wheel.y > 0 ? event.wheel.y : -event.wheel.y;
                    FOR_LOOP(i, n) {
                        Key_Event(wheelkey, CL_BindMods(SDL_GetModState()), true, event.wheel.timestamp);
                        Key_Event(wheelkey, CL_BindMods(SDL_GetModState()), false, event.wheel.timestamp);
                    }
                }
                break;
            case SDL_WINDOWEVENT:
                switch (event.window.event) {
                    case SDL_WINDOWEVENT_CLOSE:   // exit game
                        return Com_Quit();
                    default:
                        break;
                }
                break;
        }
    }
    CL_InputModeFrame();
}

void CL_SetMenuBindings(void) {
    cls.key_dest = key_menu;
    SDL_StartTextInput();
}

void CL_SetGameplayInput(void) {
    if (cls.key_dest != key_game) {
        fprintf(stderr, "CL_SetGameplayInput: switching key_dest %d -> key_game\n", cls.key_dest);
    }
    cls.key_dest = key_game;
    SDL_StopTextInput();
    CL_InputModeSetGameplay();
}

void CL_SetGameplayBindings(void) {
    CL_SetGameplayInput();
    cls.netchan.remote_address.type = NA_LOOPBACK;
}

void IN_SelectDown(void) {
    if (CL_InputModeSelectDown())
        return;
    if (!CL_GameplayInputReady()) {
        cl.selection.in_progress = false;
        return;
    }
    /* A left-click on the minimap recenters the camera instead of selecting. */
    if (CL_TryMinimapClick(mouse.origin.x, mouse.origin.y)) {
        cl.selection.in_progress = false;
        return;
    }
    cl.selection.in_progress = true;
    cl.selection.rect.x = mouse.origin.x;
    cl.selection.rect.y = mouse.origin.y;
    cl.selection.rect.w = 0;
    cl.selection.rect.h = 0;

    if (CL_MouseOverGameplayUI()) {
        cl.selection.in_progress = false;
    }
}

void IN_SelectUp(void) {
    if (CL_InputModeSelectUp())
        return;
    CL_EndMinimapDrag();
    if (!CL_GameplayInputReady()) {
        cl.selection.in_progress = false;
        return;
    }
    if (!cl.selection.in_progress)
        return;
    RECT const r = cl.selection.rect;
    cl.selection.in_progress = false;
    DWORD entnum;
    VECTOR3 point;
    if (fabs(r.w)+fabs(r.h) < 10) {
        BOOL const queue = (SDL_GetModState() & (KMOD_LSHIFT | KMOD_RSHIFT)) != 0;
        if (re.TraceEntity(&cl.viewDef, r.x, r.y, &entnum)) {
            MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
            SZ_Printf(&cls.netchan.message, queue ? "select %d queue" : "select %d", entnum);

            /* The game resolves whether this click is command targeting or a
             * selection change. Keep the local cache as a best-effort hint;
             * authoritative WC3 selection remains server-owned. */
            cl.selection.num_selected = 1;
            cl.selection.entity_nums[0] = entnum;
            CL_RequestUnitUI(1, cl.selection.entity_nums);
        } else if (re.TraceLocation(&cl.viewDef, r.x, r.y, &point)){
            MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
            SZ_Printf(&cls.netchan.message, queue ? "point %d %d queue" : "point %d %d",
                      (int)point.x, (int)point.y);
            if (cl.selection.num_selected) {
                CL_RequestUnitUI(cl.selection.num_selected, cl.selection.entity_nums);
            }
        }
    } else {
        DWORD selected[MAX_SELECTED_ENTITIES] = { 0 };
        DWORD num = re.EntitiesInRect(&cl.viewDef, &cl.selection.rect, MAX_SELECTED_ENTITIES, selected);
        char buffer[1024] = { 0 };
        if (num == 0)
            return;
        if (num > MAX_SELECTED_ENTITIES) {
            num = MAX_SELECTED_ENTITIES;
        }
        /* Shift+drag adds to the existing selection (deduped) instead of
         * replacing it, matching WC3. */
        if (SDL_GetModState() & (KMOD_LSHIFT | KMOD_RSHIFT)) {
            DWORD merged[MAX_SELECTED_ENTITIES];
            DWORD mn = 0;
            FOR_LOOP(i, cl.selection.num_selected) {
                if (mn < MAX_SELECTED_ENTITIES)
                    merged[mn++] = cl.selection.entity_nums[i];
            }
            FOR_LOOP(i, num) {
                BOOL dup = false;
                FOR_LOOP(j, mn) if (merged[j] == selected[i]) { dup = true; break; }
                if (!dup && mn < MAX_SELECTED_ENTITIES)
                    merged[mn++] = selected[i];
            }
            num = mn;
            memcpy(selected, merged, sizeof(DWORD) * mn);
        }
        strlcpy(buffer, "select", sizeof(buffer));
        FOR_LOOP(i, num) {
            size_t used = strlen(buffer);
            snprintf(buffer + used, sizeof(buffer) - used, " %d", selected[i]);
        }
        MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
        SZ_Printf(&cls.netchan.message, "%s", buffer);
        
        /* Store selected entities and request UI data (Phase 8.6) */
        cl.selection.num_selected = num;
        memcpy(cl.selection.entity_nums, selected, sizeof(DWORD) * num);
        CL_RequestUnitUI(num, cl.selection.entity_nums);
    }
}

/* Numbered control groups stored on cl.groups. Config binds `group N`. */
#define CL_GROUP_TAP_MS 500 // milliseconds; double-tap camera recenter window

static void CL_ResetGroupTap(void) {
    cl.group_last = MAX_CONTROL_GROUPS;
    cl.group_last_ms = 0;
}

static BOOL CL_GroupCenter(DWORD const *ids, DWORD n, LPVECTOR2 center) {
    double x = 0.0, y = 0.0;
    DWORD valid = 0;

    if (!ids || !center) return false;
    if (n > MAX_SELECTED_ENTITIES) n = MAX_SELECTED_ENTITIES;
    FOR_LOOP(i, n) {
        DWORD const number = ids[i];
        LPCENTITYSTATE state;
        if (!number || number >= MAX_CLIENT_ENTITIES) continue;
        state = &cl.ents[number].current;
        if (!state->model || state->stats[ENT_HEALTH] == 0 ||
            (state->flags & EF_NOT_SELECTABLE)) continue;
        x += state->origin.x;
        y += state->origin.y;
        valid++;
    }
    if (!valid) return false;
    center->x = (FLOAT)(x / valid);
    center->y = (FLOAT)(y / valid);
    return true;
}

static void CL_ApplySelection(DWORD const *ids, DWORD n) {
    char buffer[1024];
    if (n == 0) return;
    if (n > MAX_SELECTED_ENTITIES) n = MAX_SELECTED_ENTITIES;
    strlcpy(buffer, "select", sizeof(buffer));
    FOR_LOOP(i, n) {
        size_t used = strlen(buffer);
        snprintf(buffer + used, sizeof(buffer) - used, " %d", ids[i]);
    }
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s", buffer);
    cl.selection.num_selected = n;
    memcpy(cl.selection.entity_nums, ids, sizeof(DWORD) * n);
    CL_RequestUnitUI(n, cl.selection.entity_nums);
}

static void CL_GroupAssign(DWORD g) {
    DWORD n = cl.selection.num_selected;
    if (n > MAX_SELECTED_ENTITIES) n = MAX_SELECTED_ENTITIES;
    cl.groups[g].num_selected = n;
    memcpy(cl.groups[g].entity_nums, cl.selection.entity_nums, sizeof(DWORD) * n);
    CL_ResetGroupTap();
}

static void CL_GroupAdd(DWORD g) {
    DWORD n = cl.selection.num_selected;
    if (n > MAX_SELECTED_ENTITIES) n = MAX_SELECTED_ENTITIES;
    cl.groups[g].num_selected = CL_ControlGroupAppendUnique(
        cl.groups[g].entity_nums, cl.groups[g].num_selected, MAX_SELECTED_ENTITIES,
        cl.selection.entity_nums, n);
    CL_ResetGroupTap();
}

static void CL_GroupRecall(DWORD g) {
    DWORD now;
    BOOL center_on_group;
    VECTOR2 center;

    if (cl.groups[g].num_selected == 0) {
        CL_ResetGroupTap();
        return;
    }
    now = cl.time;
    center_on_group = cl.group_last == g &&
        (DWORD)(now - cl.group_last_ms) <= CL_GROUP_TAP_MS;
    CL_ApplySelection(cl.groups[g].entity_nums, cl.groups[g].num_selected);
    if (center_on_group && CL_GroupCenter(cl.groups[g].entity_nums, cl.groups[g].num_selected, &center))
        CL_SetCameraPosition(center);
    cl.group_last = g;
    cl.group_last_ms = now;
}

static void CL_Group_f(void) {
    static struct { LPCSTR name; DWORD op; } const verbs[] = {
        { "assign", 1 },
        { "add", 2 },
        { NULL, 0 },
    };
    LPCSTR a1 = Cmd_Argv(1);
    DWORD g, op = 0;

    if (!CL_GameplayInputReady() || CL_WindowModalActive()) return;
    if (Cmd_Argc() < 2) {
        fprintf(stderr, "group [assign|add] <0-9>\n");
        return;
    }
    for (DWORD i = 0; verbs[i].name; i++) {
        if (!strcasecmp(a1, verbs[i].name)) {
            op = verbs[i].op;
            a1 = Cmd_Argv(2);
            break;
        }
    }
    g = (DWORD)atoi(a1);
    if (!a1 || a1[0] < '0' || a1[0] > '9' || a1[1] || g >= MAX_CONTROL_GROUPS) {
        fprintf(stderr, "group: %s is not a group number (0-9)\n", a1 ? a1 : "");
        return;
    }
    if (op == 1) CL_GroupAssign(g);
    else if (op == 2) CL_GroupAdd(g);
    else CL_GroupRecall(g);
}

static void CL_ControlGroupsInit(void) {
    Cmd_AddCommand("group", CL_Group_f);
}

/* `zoom <delta>` — bound to MWHEELUP/MWHEELDOWN. Negative delta zooms out.
 * Clamps with camera_min_distance / camera_max_distance when max > min. */
static void CL_Zoom_f(void) {
    FLOAT steps = Cmd_Argc() > 1 ? (FLOAT)atof(Cmd_Argv(1)) : 1.0f;
    FLOAT speed = Cvar_Value("zoom_speed", 1.0f);
    FLOAT min_dist = Cvar_Value("camera_min_distance", 0.0f);
    FLOAT max_dist = Cvar_Value("camera_max_distance", 0.0f);
    FLOAT dist = cl.playerstate.distance - steps * speed;

    if (max_dist > min_dist)
        dist = MAX(min_dist, MIN(max_dist, dist));
    cl.playerstate.distance = dist;
    cl.viewDef.camerastate[0].distance = dist;
    cl.viewDef.camerastate[1].distance = dist;
}

void CL_ForwardToServer_f(void) {
    extern LPCSTR current_command;
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s", current_command+4);
}

void CL_InitInput(void) {
    fprintf(stderr, "Input initialization.\n");
    fprintf(stderr, "%d joysticks were found.\n", SDL_NumJoysticks());
    fprintf(stderr, "Input initialized.\n\n");

    Cmd_AddCommand("+select", IN_SelectDown);
    Cmd_AddCommand("-select", IN_SelectUp);
    Cmd_AddCommand("cmd", CL_ForwardToServer_f);
    Cmd_AddCommand("zoom", CL_Zoom_f);
    Cvar_Get("zoom_speed", "1.0", CVAR_ARCHIVE);
    CL_ControlGroupsInit();
    CL_InputModeInit();
}
