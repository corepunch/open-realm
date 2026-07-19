#include "cl_input_local.h"
#ifdef SC2
#include "games/starcraft-2/common/sc2_map.h"
#endif

#ifndef WOW
static struct {
    BOOL active;
    VECTOR3 anchor;
} camera_drag;

static void CL_SetCameraPosition(VECTOR2 position) {
    cl.viewDef.camerastate[0].origin.x = position.x;
    cl.viewDef.camerastate[0].origin.y = position.y;
    cl.viewDef.camerastate[1].origin.x = position.x;
    cl.viewDef.camerastate[1].origin.y = position.y;
    cl.camera_prediction.active = true;
    cl.camera_prediction.origin = position;

    MSG_WriteByte(&cls.netchan.message, clc_camera_position);
    MSG_WriteFloat(&cls.netchan.message, position.x);
    MSG_WriteFloat(&cls.netchan.message, position.y);
}

static BOOL smart_click_active;
static BOOL minimap_drag_active;

/* Left-click (or click-drag) on the minimap recenters the camera there. */
BOOL CL_TryMinimapClick(float x, float y) {
    VECTOR2 world;
    if (!CL_GameplayInputReady() || !re.TraceMinimap || !re.TraceMinimap(x, y, &world)) {
        return false;
    }
    minimap_drag_active = true;
    CL_SetCameraPosition(world);
    return true;
}

void CL_EndMinimapDrag(void) {
    minimap_drag_active = false;
}

/* --- Control groups (Ctrl+0..9 assign, 0..9 recall) ------------------------ */
static DWORD cg_ids[10][MAX_SELECTED_ENTITIES];
static DWORD cg_count[10];

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

BOOL CL_HandleGameKey(int sym, Uint16 mod) {
    if (!CL_GameplayInputReady())
        return false;
    if (sym < SDLK_0 || sym > SDLK_9)
        return false;
    DWORD const g = (DWORD)(sym - SDLK_0); /* 0..9 */
    if (mod & KMOD_CTRL) {
        /* Assign the current selection to this control group. */
        DWORD n = cl.selection.num_selected;
        if (n > MAX_SELECTED_ENTITIES) n = MAX_SELECTED_ENTITIES;
        cg_count[g] = n;
        memcpy(cg_ids[g], cl.selection.entity_nums, sizeof(DWORD) * n);
    } else {
        /* Recall the control group. */
        if (cg_count[g] > 0)
            CL_ApplySelection(cg_ids[g], cg_count[g]);
    }
    return true;
}

static void CL_BeginPan(float x, float y) {
    if (!CL_GameplayInputReady()) {
        camera_drag.active = false;
        return;
    }
    camera_drag.active = re.TraceLocation(&cl.viewDef, x, y, &camera_drag.anchor);
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
    if (!re.TraceLocation(&cl.viewDef, x, y, &point)) {
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
    DWORD entnum;
    VECTOR3 point;

    if (!CL_GameplayInputReady()) {
        return;
    }
    if (CL_MouseOverGameplayUI()) {
        return;
    }
    if (re.TraceEntity(&cl.viewDef, x, y, &entnum)) {
        MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
        SZ_Printf(&cls.netchan.message, "smart %d", entnum);
    } else if (re.TraceLocation(&cl.viewDef, x, y, &point)) {
        MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
        SZ_Printf(&cls.netchan.message, "smartpoint %d %d", (int)point.x, (int)point.y);
    }

    if (cl.selection.num_selected) {
        CL_RequestUnitUI(cl.selection.num_selected, cl.selection.entity_nums);
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

void CL_InputModeInit(void) {
    Cmd_AddCommand("+pan", IN_PanDown);
    Cmd_AddCommand("-pan", IN_PanUp);
    Cmd_AddCommand("+smart", IN_SmartDown);
    Cmd_AddCommand("-smart", IN_SmartUp);
}

void CL_InputModeSetGameplay(void) {
#ifdef SC2
    sc2MapCamera_t camera;

    SC2_MapDefaultCamera(&camera);
    cl.viewDef.camerastate[0].zfar = camera.zfar;
    cl.viewDef.camerastate[0].znear = camera.znear;
    cl.viewDef.camerastate[1].zfar = camera.zfar;
    cl.viewDef.camerastate[1].znear = camera.znear;
#else
    if (!cl.moveConfirmation)
        cl.moveConfirmation = re.LoadModel("UI\\Feedback\\Confirmation\\Confirmation.mdx");
    cl.viewDef.camerastate[0].zfar = 5000;
    cl.viewDef.camerastate[0].znear = 100;
    cl.viewDef.camerastate[1].zfar = 5000;
    cl.viewDef.camerastate[1].znear = 100;
#endif
}

void CL_InputModeMouseButton(SDL_MouseButtonEvent const *button, BOOL down) {
    if (!button || button->button != SDL_BUTTON_MIDDLE) {
        return;
    }
    if (down) {
        CL_BeginPan(button->x, button->y);
        return;
    }
    CL_EndPan();
}

void CL_InputModeMouseMotion(SDL_MouseMotionEvent const *motion) {
    if (!motion) {
        return;
    }
    if (camera_drag.active) {
        CL_UpdatePan(motion->x, motion->y);
    }
    if (minimap_drag_active) {
        VECTOR2 world;
        if (re.TraceMinimap && re.TraceMinimap(motion->x, motion->y, &world)) {
            CL_SetCameraPosition(world);
        }
    }
    if (cl.selection.in_progress) {
        cl.selection.rect.w = motion->x - cl.selection.rect.x;
        cl.selection.rect.h = motion->y - cl.selection.rect.y;
    }
}

BOOL CL_InputModeMouseWheel(SDL_MouseWheelEvent const *wheel) {
    (void)wheel;
    return false;
}

/* WC3-style camera scrolling: arrow keys and screen-edge push. Runs every
 * client frame. World +Y is north (up on screen), +X is east (right). */
#define CL_CAMERA_SCROLL_SPEED 1400.0f /* world units per second (WC3 default) */
#ifdef SC2
#undef  CL_CAMERA_SCROLL_SPEED
#define CL_CAMERA_SCROLL_SPEED 350.0f  /* SC2 world scale is smaller */
#endif
#define CL_CAMERA_EDGE_MARGIN  6        /* px from window edge that triggers scroll */

void CL_InputModeFrame(void) {
    static DWORD last_ms = 0;
    DWORD now = SDL_GetTicks();
    float dt = (last_ms && now > last_ms) ? (now - last_ms) / 1000.0f : 0.0f;
    last_ms = now;
    if (dt > 0.1f) dt = 0.1f; /* clamp after a stall */

    /* Drag-pan takes over; don't fight it. Also require in-game input. */
    if (camera_drag.active || !CL_GameplayInputReady() || dt <= 0.0f) {
        return;
    }

    float dx = 0.0f, dy = 0.0f;
    Uint8 const *keys = SDL_GetKeyboardState(NULL);
    if (keys) {
        if (keys[SDL_SCANCODE_LEFT])  dx -= 1.0f;
        if (keys[SDL_SCANCODE_RIGHT]) dx += 1.0f;
        if (keys[SDL_SCANCODE_UP])    dy += 1.0f;
        if (keys[SDL_SCANCODE_DOWN])  dy -= 1.0f;
    }

    /* Screen-edge scrolling (only while the cursor is inside the window). */
#ifndef SC2
    size2_t win = re.GetWindowSize();
    float mx = mouse.origin.x, my = mouse.origin.y;
    if (win.width > 0 && win.height > 0 &&
        mx >= 0 && my >= 0 && mx < win.width && my < win.height) {
        if (mx <= CL_CAMERA_EDGE_MARGIN)               dx -= 1.0f;
        if (mx >= (float)win.width - 1 - CL_CAMERA_EDGE_MARGIN)  dx += 1.0f;
        if (my <= CL_CAMERA_EDGE_MARGIN)               dy += 1.0f; /* top of screen = north */
        if (my >= (float)win.height - 1 - CL_CAMERA_EDGE_MARGIN) dy -= 1.0f;
    }
#endif

    if (dx == 0.0f && dy == 0.0f) {
        return;
    }

    VECTOR2 position;
    float step = CL_CAMERA_SCROLL_SPEED * dt;
    position.x = cl.viewDef.camerastate[0].origin.x + dx * step;
    position.y = cl.viewDef.camerastate[0].origin.y + dy * step;
    CL_SetCameraPosition(position);
}
#endif
