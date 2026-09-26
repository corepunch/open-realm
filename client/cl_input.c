#include "cl_input_local.h"
#include "cl_control_groups.h"
#include "ui_layout.h"

#include <stdlib.h>
#include <strings.h>

mouseEvent_t mouse;
static keyCode_t mouse_button_keys[8];
static struct {
    bool active;
    VECTOR3 anchor;
} camera_drag;

static bool smart_click_active;
#define BZ_SELECT_DOUBLE_CLICK_MS 500 // milliseconds; shared opt-in same-entity double-click window
static bool cam_west, cam_east, cam_north, cam_south;

static void CL_ScrollFrame(void);
static bool CL_MouseOverGameplayUIAt(int x, int y);

static struct {
    uint32_t buttons, sent, last_ms;
    bool select, look, focus;
    VECTOR2 down, travel;
    uint32_t last_select_entity, last_select_ms;
    SDL_Cursor *arrow, *cross, *hand;
} input = { .focus = true };

/* Commands share a typed controller contract; each game decides how its player can move. */
static void CL_SendInput(LPCINPUTCMD cmd) {
    MSG_WriteByte(&cls.netchan.message, clc_input);
    MSG_WriteInput(&cls.netchan.message, cmd);
}

/* Keep immediate orbit feedback separate from the authoritative delta-compressed player state. */
static void CL_SendView(VECTOR3 angles, float dist) {
    cl.camera_prediction.view = true;
    cl.camera_prediction.angles = angles;
    cl.camera_prediction.distance = dist;
    cl.camera_prediction.view_ms = cl.time;
    FOR_LOOP(i, 2) {
        cl.viewDef.camerastate[i].viewangles = angles;
        cl.viewDef.camerastate[i].distance = dist;
    }
    CL_SendInput(&(INPUTCMD){ .action = BZ_INPUT_VIEW, .view = { angles, dist } });
}

/* Relative travel distinguishes a context click from a drag even while SDL locks the pointer. */
static void CL_LookMotion(SDL_MouseMotionEvent const *motion) {
    if (!input.look || !CL_GameplayInputReady()) return;
    float speed = Cvar_Value("cl_mouse_speed", 0.18f);
    float lo = Cvar_Value("cl_camera_min_pitch", -85), hi = Cvar_Value("cl_camera_max_pitch", 85);
    VECTOR3 angles = cl.viewDef.camerastate[0].viewangles;
    input.travel.x += motion->xrel; input.travel.y += motion->yrel;
    angles.x = remainderf(angles.x, 360.0f);
    angles.z = remainderf(angles.z - motion->xrel * speed, 360.0f);
    angles.x = MAX(lo, MIN(hi, angles.x + motion->yrel * speed));
    CL_SendView(angles, cl.viewDef.camerastate[0].distance);
}

/* Native context cursors are an independent presentation option, unrelated to camera or selection. */
static void CL_UpdateCursor(void) {
    if (!Cvar_Integer("cl_context_cursor", 0)) return;
    if (!input.arrow) {
        input.arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
        input.cross = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
        input.hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
        if (!input.arrow || !input.cross || !input.hand)
            Com_Error(ERR_FATAL, "Input cursor creation failed: %s", SDL_GetError());
    }
    bool hostile = false;
    FOR_LOOP(i, cl.viewDef.num_entities)
        if (cl.viewDef.entities[i].number == cl.hover_entity) hostile = cl.viewDef.entities[i].flags & RF_HOSTILE;
    SDL_SetCursor(!cl.hover_entity ? input.arrow : hostile ? input.cross : input.hand);
}

static bool CL_ClickTravel(VECTOR2 delta) {
    float limit = Cvar_Value("cl_click_threshold", 10);
    return delta.x * delta.x + delta.y * delta.y <= limit * limit;
}

static void IN_LookDown(void) {
    if (!CL_GameplayInputReady() || CL_MouseOverGameplayUI()) return;
    input.look = true; input.travel = (VECTOR2){0};
    SDL_SetRelativeMouseMode(SDL_TRUE);
}

/* Config can attach a game command to a look-button click without coupling the camera to that game. */
static void IN_LookUp(void) {
    bool click = input.look && CL_ClickTravel(input.travel);
    input.look = false;
    SDL_SetRelativeMouseMode(SDL_FALSE);
    cstring_t cmd = Cvar_String("cl_look_command", "");
    if (!click || !*cmd || !CL_GameplayInputReady() || CL_MouseOverGameplayUI()) return;
    uint32_t entnum;
    if (!re.TraceEntity(&cl.viewDef, mouse.origin.x, mouse.origin.y, &entnum)) {
        if (!cl.selection.num_selected) return;
        entnum = cl.selection.entity_nums[0];
    }
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s %u", cmd, entnum);
}

/* Optional click-to-attack binding uses the same click threshold as selection and look. */
static void IN_AttackDown(void) {
    input.select = CL_GameplayInputReady() && !CL_MouseOverGameplayUI();
    input.down = mouse.origin;
}

static void IN_AttackUp(void) {
    bool held = input.select;
    input.select = false;
    if (!held || !CL_GameplayInputReady() || CL_MouseOverGameplayUI()) return;
    VECTOR2 delta = {mouse.origin.x - input.down.x, mouse.origin.y - input.down.y};
    uint32_t entnum;
    if (!CL_ClickTravel(delta) || !re.TraceEntity(&cl.viewDef, mouse.origin.x, mouse.origin.y, &entnum)) return;
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "attack %u", entnum);
}

static void IN_ForwardDown(void) { input.buttons |= BZ_MOVE_FORWARD; }
static void IN_ForwardUp(void) { input.buttons &= ~BZ_MOVE_FORWARD; }
static void IN_BackDown(void) { input.buttons |= BZ_MOVE_BACK; }
static void IN_BackUp(void) { input.buttons &= ~BZ_MOVE_BACK; }
static void IN_MoveLeftDown(void) { input.buttons |= BZ_MOVE_LEFT; }
static void IN_MoveLeftUp(void) { input.buttons &= ~BZ_MOVE_LEFT; }
static void IN_MoveRightDown(void) { input.buttons |= BZ_MOVE_RIGHT; }
static void IN_MoveRightUp(void) { input.buttons &= ~BZ_MOVE_RIGHT; }

/* Cancel held controls at ownership transitions, including a stop for a previously moving actor. */
void CL_ResetInput(void) {
    if (input.sent && cls.state == ca_active)
        CL_SendInput(&(INPUTCMD){ .action = BZ_INPUT_MOVE });
    input.buttons = input.sent = 0;
    cl.camera_prediction.active = cl.camera_prediction.view = false;
    input.select = input.look = camera_drag.active = smart_click_active = false;
    input.last_select_entity = input.last_select_ms = 0;
    cam_west = cam_east = cam_north = cam_south = false;
    cl.selection.in_progress = false;
    cl.hover_entity = 0;
    CL_EndMinimapDrag();
    if (SDL_GetRelativeMouseMode()) SDL_SetRelativeMouseMode(SDL_FALSE);
}

/* All controls run together. Bindings and individual options determine which controls are active. */
static void CL_InputFrame(void) {
    uint32_t now = SDL_GetTicks(), msec = input.last_ms ? MIN(now - input.last_ms, BZ_INPUT_MAX_MSEC) : 0;
    input.last_ms = now;
    if (!CL_GameplayInputReady()) { CL_ResetInput(); return; }
    uint32_t bits = input.buttons;
    if (Cvar_Integer("cl_move_mouse", 0) && input.select && input.look) bits |= BZ_MOVE_FORWARD;
    if (bits || input.sent) CL_SendInput(&(INPUTCMD){ .action = BZ_INPUT_MOVE, .move = { bits, msec } });
    input.sent = bits;
    CL_ScrollFrame();
}

static bool CL_OrderQueueModifierDown(void) {
    return (SDL_GetModState() & (KMOD_LSHIFT | KMOD_RSHIFT)) != 0;
}

static bool CL_IsShiftKey(int sym) {
    return sym == SDLK_LSHIFT || sym == SDLK_RSHIFT;
}

static void CL_SendOrderQueueRelease(void) {
    cstring_t command = CL_GameOrderQueueReleaseCommand();
    if (cls.state != ca_active || !command) return;
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s", command);
}

static void CL_SendOrderQueueReleaseOnShiftUp(int sym, SDL_Keymod mods) {
    if (!CL_IsShiftKey(sym) || cls.state != ca_active) return;
    /* SDL backends differ on whether KEYUP's modifier snapshot still includes
     * the key being released. Remove that key explicitly and only notify when
     * neither Shift remains held. */
    if (sym == SDLK_LSHIFT) mods &= ~KMOD_LSHIFT;
    if (sym == SDLK_RSHIFT) mods &= ~KMOD_RSHIFT;
    if (mods & (KMOD_LSHIFT | KMOD_RSHIFT)) return;
    CL_SendOrderQueueRelease();
}

static bool CL_TracePan(float x, float y, LPVECTOR3 point) {
    return Cvar_Integer("cl_camera_pan_plane", 0)
        ? re.TraceCameraPlane(&cl.viewDef, x, y, point) : re.TraceLocation(&cl.viewDef, x, y, point);
}


static void CL_BeginPan(float x, float y) {
    if (!CL_GameplayInputReady()) {
        camera_drag.active = false;
        return;
    }
    camera_drag.active = CL_TracePan(x, y, &camera_drag.anchor);
}

static void CL_UpdatePan(float x, float y) {
    VECTOR3 point;
    VECTOR2 position;

    if (!CL_GameplayInputReady()) {
        camera_drag.active = false;
        return;
    }
    if (!camera_drag.active) {
        CL_BeginPan(x, y);
        return;
    }
    if (!CL_TracePan(x, y, &point)) {
        return;
    }

    position.x = cl.viewDef.camerastate[0].origin.x + camera_drag.anchor.x - point.x;
    position.y = cl.viewDef.camerastate[0].origin.y + camera_drag.anchor.y - point.y;
    CL_SetCameraPosition(position);
}

static void CL_EndPan(void) {
    camera_drag.active = false;
}

static void CL_SendSmartCommand(float x, float y) {
    uint32_t entnum;
    VECTOR3 point;
    bool have_point = false;

    if (!CL_GameplayInputReady()) {
        return;
    }
    if (CL_MouseOverGameplayUI()) {
        return;
    }
    if (re.TraceEntity(&cl.viewDef, x, y, &entnum)) {
        /* Preserve the clicked ground point for walkable bridge fallback, but
         * keep entity picking first so repeated model clicks retain the stable
         * pre-bridge input path. */
        have_point = re.TraceLocation(&cl.viewDef, x, y, &point);
        MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
        if (have_point)
            SZ_Printf(&cls.netchan.message, CL_OrderQueueModifierDown()
                ? "smart %d %d %d queue" : "smart %d %d %d", entnum, (int)point.x, (int)point.y);
        else
            SZ_Printf(&cls.netchan.message, CL_OrderQueueModifierDown()
                ? "smart %d queue" : "smart %d", entnum);
    } else if ((have_point = re.TraceLocation(&cl.viewDef, x, y, &point))) {
        MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
        SZ_Printf(&cls.netchan.message, CL_OrderQueueModifierDown()
            ? "smartpoint %d %d queue" : "smartpoint %d %d",
            (int)point.x, (int)point.y);
    }
}

static void IN_PanDown(void) {
    if (camera_drag.active)
        return;
    CL_BeginPan(mouse.origin.x, mouse.origin.y);
}

static void IN_PanUp(void) {
    CL_EndPan();
}

static void IN_SmartDown(void) {
    if (!CL_GameplayInputReady()) {
        smart_click_active = false;
        return;
    }
    smart_click_active = true;
}

static void IN_SmartUp(void) {
    if (!CL_GameplayInputReady()) {
        smart_click_active = false;
        return;
    }
    if (!smart_click_active) {
        return;
    }
    smart_click_active = false;
    CL_SendSmartCommand(mouse.origin.x, mouse.origin.y);
}

static void IN_CamWestDown(void) { cam_west = true; }
static void IN_CamWestUp(void) { cam_west = false; }
static void IN_CamEastDown(void) { cam_east = true; }
static void IN_CamEastUp(void) { cam_east = false; }
static void IN_CamNorthDown(void) { cam_north = true; }
static void IN_CamNorthUp(void) { cam_north = false; }
static void IN_CamSouthDown(void) { cam_south = true; }
static void IN_CamSouthUp(void) { cam_south = false; }

/* `camera edge` is client-local input state. Other camera subcommands belong
 * to the game module, so forward them through the normal server command
 * path rather than duplicating camera simulation state in the client. */
static void CL_Camera_f(void) {
    if (Cmd_Argc() >= 2 && !strcasecmp(Cmd_Argv(1), "edge")) {
        if (Cmd_Argc() != 3 || (strcmp(Cmd_Argv(2), "0") && strcmp(Cmd_Argv(2), "1"))) {
            fprintf(stderr, "usage: camera edge <0|1>\n");
            return;
        }
        Cvar_Set("cl_camera_edge_scroll", Cmd_Argv(2));
        return;
    }
    if (Cmd_Argc() >= 2 && (!strcasecmp(Cmd_Argv(1), "move") ||
                            !strcasecmp(Cmd_Argv(1), "selected"))) {
        Cmd_ForwardToServer(Cmd_ArgsFrom(0));
        return;
    }
    fprintf(stderr, "usage: camera <move <x> <y>|edge <0|1>|selected>\n");
}

static void CL_RegisterCameraControls(void) {
    Cmd_AddCommand("+pan", IN_PanDown);
    Cmd_AddCommand("-pan", IN_PanUp);
    Cmd_AddCommand("+smart", IN_SmartDown);
    Cmd_AddCommand("-smart", IN_SmartUp);
    Cmd_AddCommand("+camwest", IN_CamWestDown);
    Cmd_AddCommand("-camwest", IN_CamWestUp);
    Cmd_AddCommand("+cameast", IN_CamEastDown);
    Cmd_AddCommand("-cameast", IN_CamEastUp);
    Cmd_AddCommand("+camnorth", IN_CamNorthDown);
    Cmd_AddCommand("-camnorth", IN_CamNorthUp);
    Cmd_AddCommand("+camsouth", IN_CamSouthDown);
    Cmd_AddCommand("-camsouth", IN_CamSouthUp);
    Cmd_AddCommand("camera", CL_Camera_f);
    Cvar_Get("cl_camera_edge_scroll", "0", CVAR_ARCHIVE);
    Cvar_Get("cl_camera_scroll_speed", "0", CVAR_ARCHIVE);
    Cvar_Get("cl_camera_edge_margin", "6", CVAR_ARCHIVE);
    Cvar_Get("cl_camera_pan_plane", "0", 0);
}

static bool CL_CanHoverHealthEntity(uint32_t entnum) {
    if (!entnum || entnum >= MAX_CLIENT_ENTITIES) {
        return false;
    }
    return CL_EntityAllowsWorldHover(&cl.ents[entnum].current);
}

static void CL_UpdateHover(float x, float y) {
    uint32_t entnum = 0;
    bool trace_hit = false;

    if (!CL_GameplayInputReady()) {
        cl.hover_entity = 0;
        return;
    }
    if (!CL_MouseOverGameplayUIAt((int)x, (int)y))
        trace_hit = re.TraceEntity(&cl.viewDef, x, y, &entnum);
    if (trace_hit && (!Cvar_Integer("cl_hover_health_only", 1) || CL_CanHoverHealthEntity(entnum)))
        cl.hover_entity = entnum;
    else
        cl.hover_entity = 0;
    CL_UpdateCursor();
}

static void CL_MouseMotion(SDL_MouseMotionEvent const *motion) {
    CL_LookMotion(motion);
    if (!CL_GameplayInputReady()) {
        camera_drag.active = false;
        CL_EndMinimapDrag();
        cl.selection.in_progress = false;
        cl.hover_entity = 0;
        return;
    }
    if (camera_drag.active) {
        CL_UpdatePan(motion->x, motion->y);
    }
    CL_UpdateMinimapDrag(motion->x, motion->y);
    if (cl.selection.in_progress && CL_SelectionLimit() > 1) {
        cl.selection.rect.w = motion->x - cl.selection.rect.x;
        cl.selection.rect.h = motion->y - cl.selection.rect.y;
        SCR_LayoutClampSelectionRect(&cl.selection.rect);
    }
}

/* Arrow and edge input follow the orbit yaw, so scrolling stays screen-relative after rotation. */
static void CL_ScrollFrame(void) {
    static uint32_t last_ms = 0;
    uint32_t now = SDL_GetTicks();
    float dt = (last_ms && now > last_ms) ? (now - last_ms) / 1000.0f : 0.0f;
    last_ms = now;
    if (dt > 0.1f) dt = 0.1f; /* clamp after a stall */

    /* A server-authored modal owns input completely; terminate any world drag
     * that began before the modal arrived. */
    if (!CL_GameplayInputReady()) {
        camera_drag.active = false;
        CL_EndMinimapDrag();
        cl.selection.in_progress = false;
        cl.hover_entity = 0;
        return;
    }
    /* Drag-pan takes over; don't fight it. */
    if (input.look || camera_drag.active || dt <= 0.0f) {
        return;
    }

    float dx = 0.0f, dy = 0.0f;
    if (cam_west) dx -= 1.0f;
    if (cam_east) dx += 1.0f;
    if (cam_north) dy += 1.0f;
    if (cam_south) dy -= 1.0f;

    /* Screen-edge scrolling (only while the cursor is inside the window). */
    size2_t win = re.GetWindowSize();
    float mx = mouse.origin.x, my = mouse.origin.y, margin = Cvar_Value("cl_camera_edge_margin", 6);
    if (Cvar_Value("cl_camera_edge_scroll", 0.0f) != 0.0f && win.width > 0 && win.height > 0 &&
        mx >= 0 && my >= 0 && mx < win.width && my < win.height) {
        if (mx <= margin)               dx -= 1.0f;
        if (mx >= (float)win.width - 1 - margin)  dx += 1.0f;
        if (my <= margin)               dy += 1.0f; /* top of screen = north */
        if (my >= (float)win.height - 1 - margin) dy -= 1.0f;
    }

    if (dx == 0.0f && dy == 0.0f) {
        return;
    }

    VECTOR2 position;
    float step = Cvar_Value("cl_camera_scroll_speed", 0) * dt;
    VECTOR3 dir = Vector3_rotateAroundAxis(&(VECTOR3){dx, dy, 0}, &(VECTOR3){0, 0, 1}, DEG2RAD(cl.viewDef.camerastate[0].viewangles.z));
    position.x = cl.viewDef.camerastate[0].origin.x + dir.x * step;
    position.y = cl.viewDef.camerastate[0].origin.y + dir.y * step;
    CL_SetCameraPosition(position);
}



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

static void CL_InputKeyEvent(keyCode_t key, uint32_t mods, bool down, uint32_t time) {
    Key_Event(key, mods, down, time);
    /* SDL may deliver a short press's down and up events in one poll pass;
     * execute each edge before the next event can cancel held camera state. */
    Cbuf_Execute();
}

static uint32_t CL_BindMods(SDL_Keymod m) {
    uint32_t mods = 0;
    if (m & KMOD_CTRL) mods |= KEY_MOD_CTRL;
    if (m & KMOD_ALT) mods |= KEY_MOD_ALT;
    if (m & KMOD_SHIFT) mods |= KEY_MOD_SHIFT;
    return mods;
}

/* SDL owns the authoritative window/display transition state.  Notify the
 * renderer only on events that can change the OpenGL drawable so it can
 * resync physical viewport/scissor dimensions without per-frame polling. */
static bool CL_WindowEvent(SDL_WindowEvent const *event) {
    if (event->event == SDL_WINDOWEVENT_CLOSE) {
        Com_Quit();
        return true;
    }
    switch (event->event) {
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            input.focus = true;
            break;
        case SDL_WINDOWEVENT_FOCUS_LOST:
            /* SDL may not deliver key-up events for held keys after focus is
             * lost. The server ignores this unless a Shift build chain exists. */
            CL_SendOrderQueueRelease();
            input.focus = false;
            CL_ResetInput();
            break;
        case SDL_WINDOWEVENT_MOVED:
        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
#if SDL_VERSION_ATLEAST(2, 0, 18)
        case SDL_WINDOWEVENT_DISPLAY_CHANGED:
#endif
            re.WindowChanged();
            /* Mouse events later in this poll pass hit-test against the canvas; resolve it before them. */
            CL_CanvasWindowChanged();
            break;
        default:
            break;
    }
    return false;
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

bool CL_MouseOverGameplayUI(void) {
    return CL_MouseOverGameplayUIAt((int)mouse.origin.x, (int)mouse.origin.y);
}

static bool CL_MouseOverGameplayUIAt(int x, int y) {
    return SCR_LayoutHitTest(x, y) || CL_WindowMouseOver(x, y);
}

bool CL_GameplayInputReady(void) {
    if (!input.focus || cls.key_dest != key_game || cls.state != ca_active ||
        cl.playerstate.client_ui_state != CLIENT_UI_GAME) {
        return false;
    }
    if (SCR_LayoutModalActive() || CL_WindowModalActive()) return false;
    return true;
}

void CL_Input(void) {
    SDL_Event event;
    /* Hover is presentation-only; ray-pick the last eligible motion after draining SDL input. */
    SDL_MouseMotionEvent hover_motion = { 0 };
    bool hover_update_pending = false;
    bool movie_input = CL_MovieActive();

    mouse.event = UI_EVENT_NONE;
    mouse.wheel = 0;
    while(SDL_PollEvent(&event)) {
        if (movie_input) {
            switch (event.type) {
                case SDL_KEYDOWN:
                    CL_InputKeyEvent(CL_SDLKeyToKeyCode(event.key.keysym.sym),
                                     CL_BindMods(event.key.keysym.mod), true, event.key.timestamp);
                    break;
                case SDL_KEYUP:
                    CL_InputKeyEvent(CL_SDLKeyToKeyCode(event.key.keysym.sym),
                                     CL_BindMods(event.key.keysym.mod), false, event.key.timestamp);
                    break;
                case SDL_MOUSEMOTION:
                    mouse.origin.x = event.motion.x;
                    mouse.origin.y = event.motion.y;
                    break;
                case SDL_WINDOWEVENT:
                    if (CL_WindowEvent(&event.window)) return;
                    break;
                default:
                    break;
            }
            continue;
        }
        switch(event.type) {
            case SDL_MOUSEBUTTONDOWN:
                {
                    keyCode_t mousevt = CL_MouseButtonKey(&event.button);
                    mouse.origin.x = event.button.x;
                    mouse.origin.y = event.button.y;
                    if (mousevt && cls.key_dest != key_console) {
                        mouse_button_keys[event.button.button] = mousevt;
                        CL_InputKeyEvent(mousevt, CL_BindMods(SDL_GetModState()), true, event.button.timestamp);
                    }
                }
                break;
            case SDL_MOUSEBUTTONUP:
                {
                    keyCode_t mousevt = event.button.button < sizeof(mouse_button_keys) / sizeof(*mouse_button_keys)
                                      ? mouse_button_keys[event.button.button]
                                      : 0;
                    mouse.origin.x = event.button.x;
                    mouse.origin.y = event.button.y;
                    if (mousevt && cls.key_dest != key_console) {
                        CL_InputKeyEvent(mousevt, CL_BindMods(SDL_GetModState()), false, event.button.timestamp);
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
                if (cls.key_dest == key_console) CON_TextInput(event.text.text);
                else if (CL_MenuActive() && cls.key_dest == key_menu) menu.TextInput(event.text.text);
                else if (cls.state == ca_active && cls.key_dest == key_game) CL_WindowTextInput(event.text.text);
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
                CL_InputKeyEvent(CL_SDLKeyToKeyCode(event.key.keysym.sym), CL_BindMods(event.key.keysym.mod), true, event.key.timestamp);
                break;
            case SDL_KEYUP:
                CL_SendOrderQueueReleaseOnShiftUp(event.key.keysym.sym, event.key.keysym.mod);
                if (cls.key_dest == key_console || event.key.keysym.sym == SDLK_BACKQUOTE) {
                    CON_KeyEvent(event.key.keysym.sym, false);
                    break;
                }
                CL_InputKeyEvent(CL_SDLKeyToKeyCode(event.key.keysym.sym), CL_BindMods(event.key.keysym.mod), false, event.key.timestamp);
                break;
            case SDL_MOUSEBUTTONDOWN:
                mouse.origin.x = event.button.x;
                mouse.origin.y = event.button.y;
                mouse.button = event.button.button;
                if (CL_MenuActive() && cls.key_dest == key_menu) {
                    menu.MouseEvent(MENU_MOUSE_DOWN, event.button.x, event.button.y, event.button.button);
                    break;
                }
                if (cls.state != ca_active) break;
                if (CL_WindowMouseEvent(MENU_MOUSE_DOWN, event.button.x, event.button.y, event.button.button)) break;
                if (SCR_LayoutMouseEvent(MENU_MOUSE_DOWN, event.button.x, event.button.y, event.button.button)) break;
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
                if (CL_MenuActive() && cls.key_dest == key_menu) {
                    menu.MouseEvent(MENU_MOUSE_UP, event.button.x, event.button.y, event.button.button);
                    break;
                }
                if (cls.state != ca_active) break;
                if (CL_WindowMouseEvent(MENU_MOUSE_UP, event.button.x, event.button.y, event.button.button)) break;
                if (SCR_LayoutMouseEvent(MENU_MOUSE_UP, event.button.x, event.button.y, event.button.button)) break;
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mouse.event = UI_LEFT_MOUSE_UP;
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    mouse.event = UI_RIGHT_MOUSE_UP;
                }
                break;
            case SDL_MOUSEMOTION:
                mouse.origin.x = event.motion.x;
                mouse.origin.y = event.motion.y;
                if (CL_MenuActive() && cls.key_dest == key_menu) {
                    menu.MouseEvent(MENU_MOUSE_MOVE, event.motion.x, event.motion.y, 0);
                    break;
                }
                if (cls.state != ca_active) break;
                if (CL_WindowMouseEvent(MENU_MOUSE_MOVE, event.motion.x, event.motion.y, 0)) break;
                SCR_LayoutMouseEvent(MENU_MOUSE_MOVE, event.motion.x, event.motion.y, 0);
                CL_MouseMotion(&event.motion);
                hover_motion = event.motion; hover_update_pending = true;
                break;
            case SDL_MOUSEWHEEL:
                {
                    int x, y, n;
                    keyCode_t wheelkey;
                    SDL_GetMouseState(&x, &y);
                    if (CL_MenuActive() && cls.key_dest == key_menu) {
                        menu.MouseEvent(MENU_MOUSE_SCROLL, x, y, MENU_MOUSE_PARAM(event.wheel.x, event.wheel.y));
                        break;
                    }
                    if (cls.state != ca_active) break;
                    if (CL_WindowMouseEvent(MENU_MOUSE_SCROLL, x, y, MENU_MOUSE_PARAM(event.wheel.x, event.wheel.y))) break;
                    SCR_LayoutMouseEvent(MENU_MOUSE_SCROLL, x, y, MENU_MOUSE_PARAM(event.wheel.x, event.wheel.y));
                    /* Discrete wheel ticks are bindable keys (MWHEELUP / MWHEELDOWN). */
                    if (cls.key_dest == key_console || event.wheel.y == 0)
                        break;
                    wheelkey = event.wheel.y > 0 ? K_MWHEELUP : K_MWHEELDOWN;
                    n = event.wheel.y > 0 ? event.wheel.y : -event.wheel.y;
                    FOR_LOOP(i, n) {
                        CL_InputKeyEvent(wheelkey, CL_BindMods(SDL_GetModState()), true, event.wheel.timestamp);
                        CL_InputKeyEvent(wheelkey, CL_BindMods(SDL_GetModState()), false, event.wheel.timestamp);
                    }
                }
                break;
            case SDL_WINDOWEVENT:
                if (CL_WindowEvent(&event.window)) return;
                break;
        }
    }
    if (hover_update_pending) CL_UpdateHover((float)hover_motion.x, (float)hover_motion.y);
    CL_InputFrame();
}

static void CL_SetSDLTextInput(bool enabled) {
    /* Avoid restarting an active SDL text session on every player snapshot.
     * Apart from needless churn, repeatedly toggling this can disrupt IME
     * composition on platforms that use it. */
    if (enabled) {
        if (!SDL_IsTextInputActive()) SDL_StartTextInput();
    } else if (SDL_IsTextInputActive()) {
        SDL_StopTextInput();
    }
}

void CL_SetTransientTextInput(bool enabled) {
    if (cls.key_dest != key_game) return;
    CL_SetSDLTextInput(enabled);
}

void CL_SetMenuBindings(void) {
    cls.key_dest = key_menu;
    CL_SetSDLTextInput(true);
}

void CL_SetGameplayInput(void) {
    if (cls.key_dest != key_game) {
        fprintf(stderr, "CL_SetGameplayInput: switching key_dest %d -> key_game\n", cls.key_dest);
    }
    cls.key_dest = key_game;
    /* CL_ParsePlayerInfo reaffirms gameplay input on ordinary snapshots. Do
     * not let that stop SDL_TEXTINPUT while a transient gameplay edit box
     * still owns text focus. */
    CL_SetSDLTextInput(CL_WindowTextInputActive());
}

void CL_SetGameplayBindings(void) {
    CL_SetGameplayInput();
    cls.netchan.remote_address.type = NA_LOOPBACK;
}

static void CL_ResetSelectClickChain(void) {
    input.last_select_entity = 0;
    input.last_select_ms = 0;
}

static void CL_SendSameTypeSelection(uint32_t anchor) {
    static uint32_t visible[MAX_CLIENT_ENTITIES];
    size2_t const window = re.GetWindowSize();
    rect_t const viewport = {
        .x = cl.viewDef.viewport.x * window.width,
        .y = (1.0f - (cl.viewDef.viewport.y + cl.viewDef.viewport.h)) * window.height,
        .w = cl.viewDef.viewport.w * window.width,
        .h = cl.viewDef.viewport.h * window.height,
    };
    uint32_t const visible_count = re.EntitiesInRect(&cl.viewDef, &viewport, MAX_CLIENT_ENTITIES, visible);
    char command[1024];

    if (!CL_GameBuildSameTypeSelection(&(gameSameTypeSelection_t){
            .anchor = anchor, .visible = visible, .visible_count = visible_count,
            .limit = CL_SelectionLimit(), .command = command, .command_size = sizeof(command) })) return;
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s", command);

    /* Keep only the clicked unit as a local hint until svc_set_selection
     * returns the authoritative same-type membership. */
    cl.selection.num_selected = 1;
    cl.selection.entity_nums[0] = anchor;
}

void IN_SelectDown(void) {
    input.select = false;
    input.down = mouse.origin;
    if (!CL_GameplayInputReady()) {
        CL_ResetSelectClickChain();
        cl.selection.in_progress = false;
        return;
    }
    /* Minimap focus precedes world selection and its HUD blocker, regardless of selection capacity. */
    if (CL_TryMinimapClick(mouse.origin.x, mouse.origin.y)) {
        CL_ResetSelectClickChain();
        cl.selection.in_progress = false;
        return;
    }
    if (CL_MouseOverGameplayUI()) {
        CL_ResetSelectClickChain();
        return;
    }
    input.select = true;
    if (CL_SelectionLimit() == 1) return;
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
    bool held = input.select;
    input.select = false;
    /* Release the shared drag before either selection path can return. */
    CL_EndMinimapDrag();
    if (CL_SelectionLimit() == 1) {
        if (!held || !CL_GameplayInputReady() || CL_MouseOverGameplayUI()) return;
        VECTOR2 delta = { mouse.origin.x - input.down.x, mouse.origin.y - input.down.y };
        if (!CL_ClickTravel(delta) || (input.look && !CL_ClickTravel(input.travel))) return;
        uint32_t entnum = 0;
        bool hit = re.TraceEntity(&cl.viewDef, mouse.origin.x, mouse.origin.y, &entnum);
        CL_ApplySelection(&entnum, hit ? 1 : 0);
        return;
    }
    if (!CL_GameplayInputReady()) {
        cl.selection.in_progress = false;
        return;
    }
    if (!cl.selection.in_progress)
        return;
    rect_t const r = cl.selection.rect;
    cl.selection.in_progress = false;
    uint32_t entnum;
    VECTOR3 point;
    if (fabs(r.w)+fabs(r.h) < 10) {
        SDL_Keymod const mods = SDL_GetModState();
        bool const queue = (mods & (KMOD_LSHIFT | KMOD_RSHIFT)) != 0;
        if (re.TraceEntity(&cl.viewDef, r.x, r.y, &entnum)) {
            bool const same_type_enabled = Cvar_Integer("cl_same_type_select", 0) != 0;
            bool const ctrl_same_type = same_type_enabled && !queue &&
                (mods & (KMOD_LCTRL | KMOD_RCTRL));
            bool const double_click_same_type = same_type_enabled && !queue &&
                input.last_select_entity == entnum &&
                (uint32_t)(cl.time - input.last_select_ms) < BZ_SELECT_DOUBLE_CLICK_MS;

            if (ctrl_same_type || double_click_same_type) {
                CL_SendSameTypeSelection(entnum);
            } else {
                MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
                SZ_Printf(&cls.netchan.message, queue ? "select %d queue" : "select %d", entnum);

                /* The game resolves whether this click is command targeting or a
                 * selection change. Keep the local cache as a best-effort hint;
                 * authoritative game selection remains server-owned. */
                cl.selection.num_selected = 1;
                cl.selection.entity_nums[0] = entnum;
            }
            if (queue) {
                CL_ResetSelectClickChain();
            } else {
                input.last_select_entity = entnum;
                input.last_select_ms = cl.time;
            }
        } else if (re.TraceLocation(&cl.viewDef, r.x, r.y, &point)){
            CL_ResetSelectClickChain();
            MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
            SZ_Printf(&cls.netchan.message, queue ? "point %d %d queue" : "point %d %d",
                      (int)point.x, (int)point.y);
        } else {
            CL_ResetSelectClickChain();
        }
    } else {
        CL_ResetSelectClickChain();
        uint32_t selected[MAX_SELECTED_ENTITIES] = { 0 };
        uint32_t num = re.EntitiesInRect(&cl.viewDef, &cl.selection.rect, CL_SelectionLimit(), selected);
        if (num == 0)
            return;
        if (num > CL_SelectionLimit()) {
            num = CL_SelectionLimit();
        }
        /* Shift+drag adds to the existing selection (deduped) instead of
         * replacing it, matching WC3. */
        if (SDL_GetModState() & (KMOD_LSHIFT | KMOD_RSHIFT)) {
            uint32_t merged[MAX_SELECTED_ENTITIES];
            uint32_t mn = 0;
            FOR_LOOP(i, cl.selection.num_selected) {
                if (mn < CL_SelectionLimit())
                    merged[mn++] = cl.selection.entity_nums[i];
            }
            FOR_LOOP(i, num) {
                bool dup = false;
                FOR_LOOP(j, mn) if (merged[j] == selected[i]) { dup = true; break; }
                if (!dup && mn < CL_SelectionLimit())
                    merged[mn++] = selected[i];
            }
            num = mn;
            memcpy(selected, merged, sizeof(uint32_t) * mn);
        }
        CL_ApplySelection(selected, num);
    }
}

/* `zoom <delta>` — bound to MWHEELUP/MWHEELDOWN. Negative delta zooms out.
 * Clamps with camera_min_distance / camera_max_distance when max > min. */
static void CL_Zoom_f(void) {
    float steps = Cmd_Argc() > 1 ? (float)atof(Cmd_Argv(1)) : 1.0f;
    float speed = Cvar_Value("zoom_speed", 1.0f);
    float min_dist = Cvar_Value("camera_min_distance", 0.0f);
    float max_dist = Cvar_Value("camera_max_distance", 0.0f);
    float dist = cl.viewDef.camerastate[0].distance - steps * speed;
    if (!CL_GameplayInputReady() || CL_MouseOverGameplayUI()) return;

    if (max_dist > min_dist)
        dist = MAX(min_dist, MIN(max_dist, dist));
    CL_SendView(cl.viewDef.camerastate[0].viewangles, MAX(0, dist));
}

void CL_ForwardToServer_f(void) {
    extern cstring_t current_command;
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
    CL_RegisterCameraControls();
    Cmd_AddCommand("+attack", IN_AttackDown); Cmd_AddCommand("-attack", IN_AttackUp);
    Cmd_AddCommand("+look", IN_LookDown); Cmd_AddCommand("-look", IN_LookUp);
    Cmd_AddCommand("+forward", IN_ForwardDown); Cmd_AddCommand("-forward", IN_ForwardUp);
    Cmd_AddCommand("+back", IN_BackDown); Cmd_AddCommand("-back", IN_BackUp);
    Cmd_AddCommand("+moveleft", IN_MoveLeftDown); Cmd_AddCommand("-moveleft", IN_MoveLeftUp);
    Cmd_AddCommand("+moveright", IN_MoveRightDown); Cmd_AddCommand("-moveright", IN_MoveRightUp);
    Cvar_Get("cl_selection_limit", "64", 0);
    Cvar_Get("cl_same_type_select", "0", 0);
    Cvar_Get("cl_group_focus", "1", 0);
    Cvar_Get("cl_hover_health_only", "1", 0);
    Cvar_Get("cl_context_cursor", "0", 0);
    Cvar_Get("cl_look_command", "", 0);
    Cvar_Get("cl_move_mouse", "0", 0);
    Cvar_Get("cl_mouse_speed", "0.18", CVAR_ARCHIVE);
    Cvar_Get("cl_camera_min_pitch", "-85", 0);
    Cvar_Get("cl_camera_max_pitch", "85", 0);
    Cvar_Get("cl_click_threshold", "10", 0);
    /* Old configs store pitch as wrapped negative degrees; convert the interval once at startup. */
    float lo = Cvar_Value("cl_camera_min_pitch", -85), hi = Cvar_Value("cl_camera_max_pitch", 85);
    if (lo > 180 || hi > 180) {
        if (lo > 180) lo = 360 - lo;
        if (hi > 180) hi = 360 - hi;
        Cvar_SetValue("cl_camera_min_pitch", MIN(lo, hi));
        Cvar_SetValue("cl_camera_max_pitch", MAX(lo, hi));
        fprintf(stderr, "Input: converted legacy wrapped camera pitch limits to Euler degrees\n");
    }
}

#ifdef BZ_TESTS
#include "shared/test.h"
void CL_ParseLayout(LPSIZEBUF msg);
static uint32_t pan_terrain, pan_plane;
static bool CL_TestTerrain(viewDef_t const *view, float x, float y, LPVECTOR3 point) {
    (void)view; (void)x; (void)y;
    pan_terrain++; *point = (VECTOR3){ 1, 2, 3 }; return true;
}
static bool CL_TestPlane(viewDef_t const *view, float x, float y, LPVECTOR3 point) {
    (void)view; (void)x; (void)y;
    pan_plane++; *point = (VECTOR3){ 4, 5, 6 }; return true;
}
static bool CL_TestSmartEntity(viewDef_t const *view, float x, float y, uint32_t * number) {
    (void)view; (void)x; (void)y; *number = 42; return true;
}
static bool CL_TestSelectEntity(viewDef_t const *view, float x, float y, uint32_t * number) {
    (void)view; (void)x; (void)y; *number = 7; return true;
}
static bool CL_TestSmartLocation(viewDef_t const *view, float x, float y, LPVECTOR3 point) {
    (void)view; (void)x; (void)y; *point = (VECTOR3){ 123, 456, 0 }; return true;
}
static bool CL_TestNoLocation(viewDef_t const *view, float x, float y, LPVECTOR3 point) {
    (void)view; (void)x; (void)y; (void)point; return false;
}
static bool CL_TestMinimap(float x, float y, LPVECTOR2 point) {
    (void)y; *point = (VECTOR2){ 300, 400 }; return x >= 0 && x <= 100 && y >= 0 && y <= 100;
}
static rect_t same_type_rect;
static uint32_t CL_TestEntitiesInRect(viewDef_t const *view, rect_t const * rect, uint32_t max, uint32_t * array) {
    (void)view;
    same_type_rect = *rect;
    T_ASSERT(max >= 3);
    array[0] = 7; array[1] = 8; array[2] = 9;
    return 3;
}
static bool CL_TestCameraUsesTerrainHeight(void) { return false; }
static size2_t CL_TestWindowSize(void) { return (size2_t){ 1024, 768 }; }

static int smart_trace_order;
static bool CL_TestSmartEntityOrder(viewDef_t const *view, float x, float y, uint32_t * number) {
    (void)view; (void)x; (void)y; smart_trace_order = 1; *number = 42; return true;
}
static bool CL_TestSmartLocationOrder(viewDef_t const *view, float x, float y, LPVECTOR3 point) {
    (void)view; (void)x; (void)y; T_ASSERT(smart_trace_order == 1); *point = (VECTOR3){ 123, 456, 0 }; return true;
}

/* Exercise the wire command without letting transient input state leak into later suites. */

TEST(client_input, same_type_selection_click_paths_use_visible_matching_candidates) {
    uint8_t data[256];
    __typeof__(input) old_input = input;
    __typeof__(cl.selection) old_sel = cl.selection;
    sizeBuf_t old_msg = cls.netchan.message;
    refExport_t saved = re;
    viewDef_t old_view = cl.viewDef;
    uint32_t old_time = cl.time;
    uint32_t old_class[3] = { cl.ents[7].current.class_id, cl.ents[8].current.class_id,
                           cl.ents[9].current.class_id };
    int old_state = cls.state, old_dest = cls.key_dest, old_ui = cl.playerstate.client_ui_state;
    int old_limit = Cvar_Integer("cl_selection_limit", 64);
    int old_same_type = Cvar_Integer("cl_same_type_select", 0);
    SDL_Keymod old_mod = SDL_GetModState();
    char command[128];

    re.TraceEntity = CL_TestSelectEntity; re.EntitiesInRect = CL_TestEntitiesInRect;
    re.GetWindowSize = CL_TestWindowSize;
    cls.state = ca_active; cls.key_dest = key_game; cl.playerstate.client_ui_state = CLIENT_UI_GAME;
    input = (__typeof__(input)){ .focus = true };
    Cvar_Set("cl_selection_limit", "64"); Cvar_Set("cl_same_type_select", "1");
    cl.viewDef.viewport = (rect_t){ .x = 0.1f, .y = 0.2f, .w = 0.5f, .h = 0.6f };
    cl.ents[7].current.class_id = 0x11111111u;
    cl.ents[8].current.class_id = 0x11111111u;
    cl.ents[9].current.class_id = 0x22222222u;

    /* First ordinary click establishes the double-click anchor. */
    SDL_SetModState(KMOD_NONE); cl.time = 1000;
    input.select = true; cl.selection.in_progress = true;
    cl.selection.rect = (rect_t){ .x = 200, .y = 200, .w = 0, .h = 0 };
    SZ_Init(&cls.netchan.message, data, sizeof(data)); IN_SelectUp();
    T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
    MSG_ReadString(&cls.netchan.message, command); T_STREQ(command, "select 7");

    /* A second click inside the 500 ms window expands to same-type units. */
    cl.time = 1200; input.select = true; cl.selection.in_progress = true;
    cl.selection.rect = (rect_t){ .x = 200, .y = 200, .w = 0, .h = 0 };
    SZ_Init(&cls.netchan.message, data, sizeof(data)); IN_SelectUp();
    T_FEQ(same_type_rect.x, 102.4f, 0.01f); T_FEQ(same_type_rect.y, 153.6f, 0.01f);
    T_FEQ(same_type_rect.w, 512.0f, 0.01f); T_FEQ(same_type_rect.h, 460.8f, 0.01f);
    T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
    MSG_ReadString(&cls.netchan.message, command); T_STREQ(command, "select 7 sametype 8");

    /* Ctrl+click takes the same path without needing a previous click. */
    CL_ResetSelectClickChain(); SDL_SetModState(KMOD_LCTRL); cl.time = 2000;
    input.select = true; cl.selection.in_progress = true;
    cl.selection.rect = (rect_t){ .x = 200, .y = 200, .w = 0, .h = 0 };
    SZ_Init(&cls.netchan.message, data, sizeof(data)); IN_SelectUp();
    T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
    MSG_ReadString(&cls.netchan.message, command); T_STREQ(command, "select 7 sametype 8");
    T_EQ(cl.selection.num_selected, 1); T_EQ(cl.selection.entity_nums[0], 7);

    /* Shift keeps its existing queue/toggle path instead of inventing partial
     * same-type Shift semantics in the shared client. */
    CL_ResetSelectClickChain(); SDL_SetModState(KMOD_LCTRL | KMOD_LSHIFT); cl.time = 2200;
    input.select = true; cl.selection.in_progress = true;
    cl.selection.rect = (rect_t){ .x = 200, .y = 200, .w = 0, .h = 0 };
    SZ_Init(&cls.netchan.message, data, sizeof(data)); IN_SelectUp();
    T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
    MSG_ReadString(&cls.netchan.message, command); T_STREQ(command, "select 7 queue");
    T_EQ(input.last_select_entity, 0);

    input = old_input; cl.selection = old_sel; cl.viewDef = old_view; cl.time = old_time; re = saved;
    cl.ents[7].current.class_id = old_class[0]; cl.ents[8].current.class_id = old_class[1];
    cl.ents[9].current.class_id = old_class[2]; cls.netchan.message = old_msg;
    cls.state = old_state; cls.key_dest = old_dest; cl.playerstate.client_ui_state = old_ui;
    Cvar_SetValue("cl_selection_limit", old_limit); Cvar_SetValue("cl_same_type_select", old_same_type);
    SDL_SetModState(old_mod);
}

TEST(client_input, final_shift_release_notifies_game_order_queue) {
    uint8_t data[128];
    sizeBuf_t old_msg = cls.netchan.message;
    int old_state = cls.state, old_dest = cls.key_dest, old_ui = cl.playerstate.client_ui_state;
    bool old_focus = input.focus;
    SDL_Keymod old_mod = SDL_GetModState();
    cstring_t release_command = CL_GameOrderQueueReleaseCommand();
    char command[64];

    cls.state = ca_active;
    cls.key_dest = key_game;
    cl.playerstate.client_ui_state = CLIENT_UI_GAME;
    input.focus = true;

    SDL_SetModState(KMOD_NONE);
    SZ_Init(&cls.netchan.message, data, sizeof(data));
    CL_SendOrderQueueReleaseOnShiftUp(SDLK_LSHIFT, KMOD_LSHIFT);
    if (release_command) {
        T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
        MSG_ReadString(&cls.netchan.message, command);
        T_STREQ(command, release_command);
    } else T_EQ(cls.netchan.message.cursize, 0);

    SDL_SetModState(KMOD_RSHIFT);
    SZ_Init(&cls.netchan.message, data, sizeof(data));
    CL_SendOrderQueueReleaseOnShiftUp(SDLK_LSHIFT, KMOD_LSHIFT | KMOD_RSHIFT);
    T_EQ(cls.netchan.message.cursize, 0);

    SDL_SetModState(KMOD_NONE);
    SZ_Init(&cls.netchan.message, data, sizeof(data));
    CL_SendOrderQueueReleaseOnShiftUp(SDLK_a, KMOD_NONE);
    T_EQ(cls.netchan.message.cursize, 0);

    cls.netchan.message = old_msg;
    cls.state = old_state;
    cls.key_dest = old_dest;
    cl.playerstate.client_ui_state = old_ui;
    input.focus = old_focus;
    SDL_SetModState(old_mod);
}

static void CL_TestOrderQueueReleaseMessage(void) {
    cstring_t command = CL_GameOrderQueueReleaseCommand();
    char text[64];
    if (!command) { T_EQ(cls.netchan.message.cursize, 0); return; }
    T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
    MSG_ReadString(&cls.netchan.message, text);
    T_STREQ(text, command);
}

TEST(client_input, shift_release_reaches_game_when_console_owns_keyup) {
    uint8_t data[128];
    struct client_state *old_cl = MemAlloc(sizeof(cl));
    struct client_static old_cls = cls;
    __typeof__(input) old_input = input;
    mouseEvent_t old_mouse = mouse;
    SDL_Event event = { .key = { .type = SDL_KEYUP, .keysym.sym = SDLK_LSHIFT, .keysym.mod = KMOD_LSHIFT } };
    SDL_Keymod old_mod = SDL_GetModState();

    memcpy(old_cl, &cl, sizeof(cl)); memset(&cl, 0, sizeof(cl));
    T_EQ(SDL_InitSubSystem(SDL_INIT_EVENTS), 0); SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    input = (__typeof__(input)){ .focus = true };
    cls.state = ca_active; cls.key_dest = key_console; cl.playerstate.client_ui_state = CLIENT_UI_GAME;
    SZ_Init(&cls.netchan.message, data, sizeof(data));
    T_EQ(SDL_PushEvent(&event), 1); CL_Input();
    CL_TestOrderQueueReleaseMessage();

    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT); SDL_QuitSubSystem(SDL_INIT_EVENTS);
    cl = *old_cl; MemFree(old_cl); cls = old_cls; input = old_input; mouse = old_mouse;
    SDL_SetModState(old_mod);
}

TEST(client_input, focus_loss_releases_game_order_queue) {
    uint8_t data[128];
    struct client_state *old_cl = MemAlloc(sizeof(cl));
    struct client_static old_cls = cls;
    __typeof__(input) old_input = input;
    mouseEvent_t old_mouse = mouse;
    SDL_Event event = { .window = { .type = SDL_WINDOWEVENT, .event = SDL_WINDOWEVENT_FOCUS_LOST } };
    SDL_Keymod old_mod = SDL_GetModState();

    memcpy(old_cl, &cl, sizeof(cl)); memset(&cl, 0, sizeof(cl));
    T_EQ(SDL_InitSubSystem(SDL_INIT_EVENTS), 0); SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    input = (__typeof__(input)){ .focus = true };
    cls.state = ca_active; cls.key_dest = key_game; cl.playerstate.client_ui_state = CLIENT_UI_GAME;
    SZ_Init(&cls.netchan.message, data, sizeof(data));
    T_EQ(SDL_PushEvent(&event), 1); CL_Input();
    CL_TestOrderQueueReleaseMessage();
    T_ASSERT(!input.focus);

    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT); SDL_QuitSubSystem(SDL_INIT_EVENTS);
    cl = *old_cl; MemFree(old_cl); cls = old_cls; input = old_input; mouse = old_mouse;
    SDL_SetModState(old_mod);
}

TEST(client_input, smart_entity_click_preserves_ground_point) {
    uint8_t data[256];
    __typeof__(cl.selection) old_sel = cl.selection;
    sizeBuf_t old_msg = cls.netchan.message;
    refExport_t saved = re;
    int old_state = cls.state, old_dest = cls.key_dest, old_ui = cl.playerstate.client_ui_state;
    bool old_focus = input.focus;
    SDL_Keymod old_mod = SDL_GetModState();
    char command[128];

    re.TraceEntity = CL_TestSmartEntity; re.TraceLocation = CL_TestSmartLocation;
    re.GetWindowSize = CL_TestWindowSize;
    cls.state = ca_active; cls.key_dest = key_game; cl.playerstate.client_ui_state = CLIENT_UI_GAME;
    input.focus = true; cl.selection.num_selected = 0;
    FOR_LOOP(i, 2) {
        SDL_SetModState(i ? KMOD_LSHIFT : KMOD_NONE);
        SZ_Init(&cls.netchan.message, data, sizeof(data));
        CL_SendSmartCommand(10, 20);
        T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
        MSG_ReadString(&cls.netchan.message, command);
        T_STREQ(command, i ? "smart 42 123 456 queue" : "smart 42 123 456");
        T_EQ(cls.netchan.message.readcount, cls.netchan.message.cursize);
    }
    SDL_SetModState(KMOD_NONE);
    SZ_Init(&cls.netchan.message, data, sizeof(data)); re.TraceLocation = CL_TestNoLocation;
    CL_SendSmartCommand(10, 20);
    T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
    MSG_ReadString(&cls.netchan.message, command);
    T_STREQ(command, "smart 42");
    cl.selection = old_sel; re = saved; cls.netchan.message = old_msg;
    cls.state = old_state; cls.key_dest = old_dest; input.focus = old_focus;
    cl.playerstate.client_ui_state = old_ui; SDL_SetModState(old_mod);
}

TEST(client_input, smart_entity_trace_precedes_ground_trace) {
    uint8_t data[256];
    refExport_t saved = re;
    sizeBuf_t old_msg = cls.netchan.message;
    int old_state = cls.state, old_dest = cls.key_dest, old_ui = cl.playerstate.client_ui_state;
    bool old_focus = input.focus;

    re.TraceEntity = CL_TestSmartEntityOrder; re.TraceLocation = CL_TestSmartLocationOrder;
    re.GetWindowSize = CL_TestWindowSize;
    cls.state = ca_active; cls.key_dest = key_game; cl.playerstate.client_ui_state = CLIENT_UI_GAME;
    input.focus = true; smart_trace_order = 0;
    SZ_Init(&cls.netchan.message, data, sizeof(data));
    CL_SendSmartCommand(10, 20);
    T_EQ(smart_trace_order, 1);
    re = saved; cls.netchan.message = old_msg; cls.state = old_state; cls.key_dest = old_dest;
    cl.playerstate.client_ui_state = old_ui; input.focus = old_focus;
}

/* A remote client owns its collision world; input tests cannot borrow a previous game-module fixture. */
static void CL_TestWorldBounds(bool set) {
#ifdef BZ_CLIENT_WORLD
    extern void CM_SetupTestWorldBounds(LPCBOX2 bounds);
    BOX2 bounds = { .min = { 0, 0 }, .max = { 1024, 768 } };
    CM_SetupTestWorldBounds(set ? &bounds : NULL);
#else
    (void)set;
#endif
}

TEST(client_input, quick_arrow_press_is_sampled_before_release) {
    uint8_t data[256];
    struct client_state old_cl = cl;
    struct client_static old_cls = cls;
    refExport_t saved = re;
    __typeof__(input) old_input = input;
    int old_state = cls.state, old_dest = cls.key_dest, old_ui = cl.playerstate.client_ui_state;
    float old_speed = Cvar_Value("cl_camera_scroll_speed", 0);
    bool add_down = !Cmd_Exists("+camwest"), add_up = !Cmd_Exists("-camwest");
    UINAME old_bind;

    CL_TestWorldBounds(true);
    if (add_down) Cmd_AddCommand("+camwest", IN_CamWestDown);
    if (add_up) Cmd_AddCommand("-camwest", IN_CamWestUp);
    strlcpy(old_bind, Key_GetBinding(K_LEFTARROW, 0), sizeof(old_bind));
    Key_SetBinding(K_LEFTARROW, 0, "+camwest");
    re.GetWindowSize = CL_TestWindowSize; re.CameraUsesTerrainHeight = CL_TestCameraUsesTerrainHeight;
    cls.state = ca_active; cls.key_dest = key_game; cl.playerstate.client_ui_state = CLIENT_UI_GAME;
    cl.viewDef.camerastate[0].origin = (VECTOR3){0}; cl.viewDef.camerastate[0].viewangles = (VECTOR3){0};
    input = (__typeof__(input)){ .focus = true, .last_ms = SDL_GetTicks() - 16 };
    Cvar_SetValue("cl_camera_scroll_speed", 1400);
    SZ_Init(&cls.netchan.message, data, sizeof(data));
    CL_InputKeyEvent(K_LEFTARROW, 0, true, 0); CL_Input();
    T_ASSERT(cam_west);
    T_STREQ(Key_GetBinding(K_LEFTARROW, 0), "+camwest");
    Cbuf_AddText("-camwest\n"); Cbuf_Execute();
    T_ASSERT(!cam_west);

    cl = old_cl; cls = old_cls; re = saved; input = old_input;
    cls.state = old_state; cls.key_dest = old_dest; cl.playerstate.client_ui_state = old_ui;
    Cvar_SetValue("cl_camera_scroll_speed", old_speed);
    if (add_down) Cmd_RemoveCommand("+camwest");
    if (add_up) Cmd_RemoveCommand("-camwest");
    Key_SetBinding(K_LEFTARROW, 0, old_bind);
    CL_TestWorldBounds(false);
}

/* Minimap focus is shared input: selection capacity cannot change its packet or drag lifecycle. */
TEST(client_input, minimap_focus_and_release_are_selection_independent) {
    CL_TestWorldBounds(true);
    uint8_t data[256];
    __typeof__(cl.selection) old_sel = cl.selection;
    __typeof__(cl.camera_prediction) old_pred = cl.camera_prediction;
    __typeof__(input) old_input = input;
    viewDef_t old_view = cl.viewDef;
    mouseEvent_t old_mouse = mouse;
    sizeBuf_t old_msg = cls.netchan.message;
    refExport_t saved = re;
    int old_state = cls.state, old_dest = cls.key_dest, old_ui = cl.playerstate.client_ui_state;
    int old_limit = Cvar_Integer("cl_selection_limit", 64);
    VECTOR2 expected = CL_ClampCameraPosition((VECTOR2){ 300, 400 });
    INPUTCMD cmd;

    re.TraceMinimap = CL_TestMinimap; re.GetWindowSize = CL_TestWindowSize;
    re.CameraUsesTerrainHeight = CL_TestCameraUsesTerrainHeight;
    cls.state = ca_active; cls.key_dest = key_game; cl.playerstate.client_ui_state = CLIENT_UI_GAME;
    input.focus = true; input.select = false; cl.selection.in_progress = false;
    mouse.origin = (VECTOR2){ 10, 20 };
    FOR_LOOP(i, 2) {
        Cvar_SetValue("cl_selection_limit", i ? 64 : 1);
        SZ_Init(&cls.netchan.message, data, sizeof(data));
        IN_SelectDown();
        T_ASSERT(!input.select && !cl.selection.in_progress);
        T_EQ(MSG_ReadByte(&cls.netchan.message), clc_input);
        T_ASSERT(MSG_ReadInput(&cls.netchan.message, &cmd));
        T_EQ(cmd.action, BZ_INPUT_FOCUS);
        T_FEQ(cmd.focus.x, expected.x, 0.001f); T_FEQ(cmd.focus.y, expected.y, 0.001f);
        T_EQ(cls.netchan.message.readcount, cls.netchan.message.cursize);
        FOR_LOOP(j, 2) {
            T_FEQ(cl.viewDef.camerastate[j].origin.x, expected.x, 0.001f);
            T_FEQ(cl.viewDef.camerastate[j].origin.y, expected.y, 0.001f);
        }
        SZ_Init(&cls.netchan.message, data, sizeof(data));
        CL_UpdateMinimapDrag(30, 40);
        T_EQ(MSG_ReadByte(&cls.netchan.message), clc_input);
        T_ASSERT(MSG_ReadInput(&cls.netchan.message, &cmd));
        T_EQ(cmd.action, BZ_INPUT_FOCUS);
        IN_SelectUp();
        SZ_Init(&cls.netchan.message, data, sizeof(data));
        CL_UpdateMinimapDrag(50, 60);
        T_EQ(cls.netchan.message.cursize, 0);
        T_ASSERT(!CL_TryMinimapClick(-1, 20));
        CL_UpdateMinimapDrag(50, 60);
        T_EQ(cls.netchan.message.cursize, 0);
        cls.key_dest = key_menu;
        IN_SelectDown(); IN_SelectUp();
        T_EQ(cls.netchan.message.cursize, 0);
        cls.key_dest = key_game;
    }
    cl.selection = old_sel; cl.camera_prediction = old_pred; cl.viewDef = old_view;
    input = old_input; mouse = old_mouse;
    Cvar_SetValue("cl_selection_limit", old_limit);
    re = saved; cls.netchan.message = old_msg; cls.state = old_state; cls.key_dest = old_dest;
    cl.playerstate.client_ui_state = old_ui;
    CL_TestWorldBounds(false);
}

static uint32_t hover_trace_calls;
static VECTOR2 hover_trace_point;
static bool CL_TestHoverEntity(viewDef_t const *view, float x, float y, uint32_t * number) {
    (void)view; hover_trace_calls++; hover_trace_point = (VECTOR2){ x, y }; *number = 7; return true;
}

TEST(client_input, hover_trace_coalesces_mouse_motion_in_input_pump) {
    struct client_state *old_cl = MemAlloc(sizeof(cl));
    struct client_static old_cls = cls;
    refExport_t saved = re;
    __typeof__(input) old_input = input;
    mouseEvent_t old_mouse = mouse;
    float old_hover_only = Cvar_Value("cl_hover_health_only", 1);
    float old_context_cursor = Cvar_Value("cl_context_cursor", 0);
    SDL_Event events[] = {
        { .motion = { .type = SDL_MOUSEMOTION, .x = 101, .y = 202 } },
        { .motion = { .type = SDL_MOUSEMOTION, .x = 303, .y = 404 } },
        { .motion = { .type = SDL_MOUSEMOTION, .x = 505, .y = 606 } },
    };

    memcpy(old_cl, &cl, sizeof(cl)); memset(&cl, 0, sizeof(cl));
    T_EQ(SDL_InitSubSystem(SDL_INIT_EVENTS), 0); SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    input = (__typeof__(input)){ .focus = true };
    cls.state = ca_active; cls.key_dest = key_game; cl.playerstate.client_ui_state = CLIENT_UI_GAME;
    re.TraceEntity = CL_TestHoverEntity; re.GetWindowSize = CL_TestWindowSize;
    Cvar_SetValue("cl_hover_health_only", 0); Cvar_SetValue("cl_context_cursor", 0);
    hover_trace_calls = 0; hover_trace_point = (VECTOR2){ 0 };
    FOR_LOOP(i, sizeof(events) / sizeof(events[0])) T_EQ(SDL_PushEvent(&events[i]), 1);
    CL_Input();
    T_EQ(hover_trace_calls, 1); T_FEQ(hover_trace_point.x, 505, 0.001f);
    T_FEQ(hover_trace_point.y, 606, 0.001f); T_EQ(cl.hover_entity, 7);

    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT); SDL_QuitSubSystem(SDL_INIT_EVENTS);
    cl = *old_cl; MemFree(old_cl); cls = old_cls; re = saved; input = old_input; mouse = old_mouse;
    Cvar_SetValue("cl_hover_health_only", old_hover_only); Cvar_SetValue("cl_context_cursor", old_context_cursor);
}

/* Keep the SDL queue, key binding, layout hit test and command buffer in the regression path. */
static uint32_t test_menu_mouse, test_menu_text, test_menu_keys;
static bool CL_TestMenuMouse(menuMouseEvent_t event, int x, int y, int32_t param) {
    (void)event; (void)x; (void)y; (void)param; test_menu_mouse++; return false;
}
static void CL_TestMenuText(cstring_t text) { (void)text; test_menu_text++; }
static void CL_TestMenuKey(int key, bool down, uint32_t time) { (void)key; (void)down; (void)time; test_menu_keys++; }
TEST(client_input, menu_sdl_input_is_exclusive_with_world_presentation) {
    struct client_state *old = MemAlloc(sizeof(cl));
    struct client_static old_cls = cls;
    menuExport_t old_menu = menu;
    UINAME click_bind, key_bind;
    mouseEvent_t old_mouse = mouse;
    refExport_t old_re = re;
    __typeof__(input) old_input = input;
    SDL_Event events[] = {
        { .button = { .type = SDL_MOUSEBUTTONDOWN, .button = SDL_BUTTON_LEFT, .x = 10, .y = 20 } },
        { .button = { .type = SDL_MOUSEBUTTONUP, .button = SDL_BUTTON_LEFT, .x = 10, .y = 20 } },
        { .motion = { .type = SDL_MOUSEMOTION, .x = 10, .y = 20 } },
        { .wheel = { .type = SDL_MOUSEWHEEL, .y = 1 } },
        { .text = { .type = SDL_TEXTINPUT, .text = "test" } },
        { .key = { .type = SDL_KEYDOWN, .keysym.sym = SDLK_F12 } },
    };
    memcpy(old, &cl, sizeof(cl)); memset(&cl, 0, sizeof(cl));
    input = (__typeof__(input)){0}; cls.key_dest = key_menu;
    menu.MouseEvent = CL_TestMenuMouse; menu.TextInput = CL_TestMenuText; menu.KeyEvent = CL_TestMenuKey;
    re.GetWindowSize = CL_TestWindowSize;
    strlcpy(click_bind, Key_GetBinding(K_MOUSE1, 0), sizeof(click_bind));
    strlcpy(key_bind, Key_GetBinding(K_F12, 0), sizeof(key_bind));
    Key_SetBinding(K_MOUSE1, 0, ""); Key_SetBinding(K_F12, 0, "");
    T_EQ(SDL_InitSubSystem(SDL_INIT_EVENTS), 0);
    test_menu_mouse = test_menu_text = test_menu_keys = 0;
    FOR_LOOP(mode, 3) {
        cls.state = mode == 0 ? ca_active : mode == 1 ? ca_connected : ca_disconnected;
        cl.playerstate.client_ui_state = mode == 1 ? CLIENT_UI_LOADING : CLIENT_UI_GAME;
        FOR_LOOP(i, sizeof(events) / sizeof(events[0])) T_EQ(SDL_PushEvent(&events[i]), 1);
        CL_Input();
        if (mode < 2) {
            T_EQ(test_menu_mouse, 0); T_EQ(test_menu_text, 0); T_EQ(test_menu_keys, 0);
        } else {
            T_EQ(test_menu_mouse, 4); T_EQ(test_menu_text, 1); T_ASSERT(test_menu_keys > 0);
        }
    }
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
    Key_SetBinding(K_MOUSE1, 0, click_bind); Key_SetBinding(K_F12, 0, key_bind); mouse = old_mouse;
    memcpy(&cl, old, sizeof(cl)); MemFree(old); cls = old_cls; menu = old_menu; re = old_re; input = old_input;
}

TEST(client_input, minimap_sdl_click_drag_release_over_hud) {
    CL_TestWorldBounds(true);
    struct client_state *old_cl = MemAlloc(sizeof(cl));
    struct client_static old_cls = cls;
    refExport_t old_re = re;
    __typeof__(input) old_input = input;
    mouseEvent_t old_mouse = mouse;
    SDL_Keymod old_mod = SDL_GetModState();
    UINAME binding;
    uint8_t data[512], packet[512];
    sizeBuf_t msg;
    UIFRAME empty = { 0 }, frame = { .number = 1, .flags.type = FT_TEXTURE,
        .size = { UI_BASE_WIDTH, UI_BASE_HEIGHT }, .tooltip = "Minimap" };
    SDL_Event event = { .button = { .type = SDL_MOUSEBUTTONDOWN, .button = SDL_BUTTON_LEFT, .x = 10, .y = 20 } };
    float old_edge = Cvar_Value("cl_camera_edge_scroll", 0), old_cursor = Cvar_Value("cl_context_cursor", 0);
    bool add_down = !Cmd_Exists("+select"), add_up = !Cmd_Exists("-select");
    INPUTCMD cmd = { 0 };

    memcpy(old_cl, &cl, sizeof(cl));
    strlcpy(binding, Key_GetBinding(K_MOUSE1, 0), sizeof(binding));
    T_EQ(SDL_InitSubSystem(SDL_INIT_EVENTS), 0);
    if (add_down) Cmd_AddCommand("+select", IN_SelectDown);
    if (add_up) Cmd_AddCommand("-select", IN_SelectUp);
    Key_SetBinding(K_MOUSE1, 0, "+select"); SDL_SetModState(KMOD_NONE);
    Cvar_Set("cl_camera_edge_scroll", "0"); Cvar_Set("cl_context_cursor", "0");
    memset(&cl, 0, sizeof(cl)); input = (__typeof__(input)){ .focus = true };
    cls.state = ca_active; cls.key_dest = key_game; cl.playerstate.client_ui_state = CLIENT_UI_GAME;
    re.TraceMinimap = CL_TestMinimap; re.GetWindowSize = CL_TestWindowSize;
    re.CameraUsesTerrainHeight = CL_TestCameraUsesTerrainHeight;
    FOR_LOOP(i, MAX_LAYOUT_LAYERS) SCR_ClearLayoutLayer(i);
    SZ_Init(&msg, packet, sizeof(packet));
    MSG_WriteByte(&msg, LAYER_CONSOLE);
    MSG_WriteDeltaUIFrame(&msg, &empty, &frame, true); MSG_WriteByte(&msg, 0);
    MSG_WriteLong(&msg, 0); MSG_WriteShort(&msg, 0);
    CL_ParseLayout(&msg);
    T_ASSERT(SCR_LayoutHitTest(10, 20));
    SZ_Init(&cls.netchan.message, data, sizeof(data));
    T_EQ(SDL_PushEvent(&event), 1); CL_Input(); Cbuf_Execute();
    T_EQ(MSG_ReadByte(&cls.netchan.message), clc_input);
    T_ASSERT(MSG_ReadInput(&cls.netchan.message, &cmd)); T_EQ(cmd.action, BZ_INPUT_FOCUS);
    VECTOR2 expected = CL_ClampCameraPosition((VECTOR2){ 300, 400 });
    T_FEQ(cmd.focus.x, expected.x, 0.001f); T_FEQ(cmd.focus.y, expected.y, 0.001f);
    T_ASSERT(!input.select && !cl.selection.in_progress);
    T_EQ(cls.netchan.message.readcount, cls.netchan.message.cursize);

    event = (SDL_Event){ .motion = { .type = SDL_MOUSEMOTION, .x = 30, .y = 40 } };
    SZ_Init(&cls.netchan.message, data, sizeof(data));
    T_EQ(SDL_PushEvent(&event), 1); CL_Input(); Cbuf_Execute();
    T_EQ(MSG_ReadByte(&cls.netchan.message), clc_input);
    T_ASSERT(MSG_ReadInput(&cls.netchan.message, &cmd)); T_EQ(cmd.action, BZ_INPUT_FOCUS);
    T_EQ(cls.netchan.message.readcount, cls.netchan.message.cursize);
    event = (SDL_Event){ .button = { .type = SDL_MOUSEBUTTONUP, .button = SDL_BUTTON_LEFT, .x = 30, .y = 40 } };
    T_EQ(SDL_PushEvent(&event), 1); CL_Input(); Cbuf_Execute();
    SZ_Init(&cls.netchan.message, data, sizeof(data));
    event = (SDL_Event){ .motion = { .type = SDL_MOUSEMOTION, .x = 50, .y = 60 } };
    T_EQ(SDL_PushEvent(&event), 1); CL_Input(); Cbuf_Execute();
    T_EQ(cls.netchan.message.cursize, 0);

    /* Other HUD pixels must not become world selection or camera focus. */
    event = (SDL_Event){ .button = { .type = SDL_MOUSEBUTTONDOWN, .button = SDL_BUTTON_LEFT, .x = 500, .y = 100 } };
    T_ASSERT(SCR_LayoutHitTest(500, 100));
    T_EQ(SDL_PushEvent(&event), 1); CL_Input(); Cbuf_Execute();
    event.type = SDL_MOUSEBUTTONUP;
    T_EQ(SDL_PushEvent(&event), 1); CL_Input(); Cbuf_Execute();
    T_EQ(cls.netchan.message.cursize, 0); T_ASSERT(!cl.selection.in_progress);

    /* A real modal layout must still prevent the same bound click from moving the camera. */
    SCR_SetLayoutLayer(LAYER_GAME_RESULT, cl.layout[LAYER_CONSOLE]);
    event = (SDL_Event){ .button = { .type = SDL_MOUSEBUTTONDOWN, .button = SDL_BUTTON_LEFT, .x = 10, .y = 20 } };
    T_ASSERT(SCR_LayoutModalActive());
    T_EQ(SDL_PushEvent(&event), 1); CL_Input(); Cbuf_Execute();
    event.type = SDL_MOUSEBUTTONUP;
    T_EQ(SDL_PushEvent(&event), 1); CL_Input(); Cbuf_Execute();
    T_EQ(cls.netchan.message.cursize, 0);
    MemFree(cl.layout[LAYER_CONSOLE]);
    cl = *old_cl; MemFree(old_cl); cls = old_cls; re = old_re; input = old_input; mouse = old_mouse;
    FOR_LOOP(i, MAX_LAYOUT_LAYERS) SCR_SetLayoutLayer(i, cl.layout[i]);
    Key_SetBinding(K_MOUSE1, 0, binding); SDL_SetModState(old_mod);
    if (add_down) Cmd_RemoveCommand("+select");
    if (add_up) Cmd_RemoveCommand("-select");
    Cvar_SetValue("cl_camera_edge_scroll", old_edge); Cvar_SetValue("cl_context_cursor", old_cursor);
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
    CL_TestWorldBounds(false);
}

TEST(client_input, pan_uses_configured_surface) {
    refExport_t saved = re;
    float old = Cvar_Value("cl_camera_pan_plane", 0);
    VECTOR3 point;
    re.TraceLocation = CL_TestTerrain;
    re.TraceCameraPlane = CL_TestPlane;
    pan_terrain = pan_plane = 0;
    Cvar_Set("cl_camera_pan_plane", "0");
    T_ASSERT(CL_TracePan(0, 0, &point));
    T_EQ(pan_terrain, 1); T_EQ(pan_plane, 0); T_FEQ(point.z, 3, 0.001f);
    Cvar_Set("cl_camera_pan_plane", "1");
    T_ASSERT(CL_TracePan(0, 0, &point));
    T_EQ(pan_terrain, 1); T_EQ(pan_plane, 1); T_FEQ(point.z, 6, 0.001f);
    Cvar_SetValue("cl_camera_pan_plane", old);
    re = saved;
}
#endif

#ifdef BZ_TESTS
/* Losing gameplay ownership sends a release once and terminates every held control. */
TEST(client_input, modal_releases_movement_and_drags) {
    uint8_t data[64];
    sizeBuf_t old_msg = cls.netchan.message;
    int old_state = cls.state, old_dest = cls.key_dest;
    INPUTCMD cmd;
    SZ_Init(&cls.netchan.message, data, sizeof(data));
    cls.state = ca_active; cls.key_dest = key_menu;
    input.buttons = input.sent = BZ_MOVE_FORWARD;
    input.select = input.look = camera_drag.active = smart_click_active = true;
    cl.selection.in_progress = true; cam_west = true;
    CL_InputFrame();
    T_EQ(MSG_ReadByte(&cls.netchan.message), clc_input);
    T_ASSERT(MSG_ReadInput(&cls.netchan.message, &cmd));
    T_EQ(cmd.action, BZ_INPUT_MOVE); T_EQ(cmd.move.buttons, 0);
    T_ASSERT(!input.look && !input.select && !camera_drag.active && !smart_click_active && !cam_west);
    T_ASSERT(!cl.selection.in_progress);
    uint32_t size = cls.netchan.message.cursize;
    CL_InputFrame(); T_EQ(cls.netchan.message.cursize, size);
    cls.netchan.message = old_msg; cls.state = old_state; cls.key_dest = old_dest;
}
#endif
