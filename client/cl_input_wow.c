#include "cl_input_local.h"

#ifdef WOW
#define WOW_MOVE_FORWARD 1
#define WOW_MOVE_BACK 2
#define WOW_MOVE_LEFT 4
#define WOW_MOVE_RIGHT 8
#define WOW_CAMERA_MIN_PITCH 300.0f
#define WOW_CAMERA_MAX_PITCH 350.0f
#define WOW_CAMERA_MIN_DISTANCE 3.0f
#define WOW_CAMERA_MAX_DISTANCE 35.0f
#define WOW_CAMERA_TURN_SPEED 135.0f
#define WOW_MOUSE_TURN_SPEED 0.18f
#define WOW_CLICK_THRESHOLD 10  /* px movement before a click becomes a drag */

static struct {
    BOOL initialized;
    BOOL right_mouse;
    BOOL left_mouse;
    BOOL move_forward;
    BOOL move_back;
    BOOL move_left;
    BOOL move_right;
    DWORD last_time;
    FLOAT yaw;
    FLOAT pitch;
    FLOAT distance;
    /* Click-vs-drag tracking for LMB and RMB. */
    BOOL lmb_down;
    BOOL rmb_dragging;
    VECTOR2 lmb_down_pos;
    VECTOR2 rmb_down_pos;
    /* Context cursors. */
    SDL_Cursor *cursor_arrow;
    SDL_Cursor *cursor_crosshair;
    SDL_Cursor *cursor_hand;
    DWORD last_hover_entity;
} wow_input = {
    .pitch = 328.0f,
    .distance = 8.5f,
};

static FLOAT CL_WowClamp(FLOAT value, FLOAT min_value, FLOAT max_value) {
    return MAX(min_value, MIN(value, max_value));
}

static void CL_WowInitInputState(void) {
    if (wow_input.initialized) {
        return;
    }
    wow_input.initialized = true;
    wow_input.last_time = SDL_GetTicks();
    wow_input.pitch = 328.0f;
    wow_input.distance = 8.5f;
    wow_input.cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    wow_input.cursor_crosshair = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    wow_input.cursor_hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    wow_input.last_hover_entity = 0;
}

static BOOL CL_WowMouseMovedPast(VECTOR2 const *a, VECTOR2 const *b) {
    FLOAT dx = a->x - b->x;
    FLOAT dy = a->y - b->y;
    return (dx * dx + dy * dy) > (WOW_CLICK_THRESHOLD * WOW_CLICK_THRESHOLD);
}

static void CL_WowSendSelect(DWORD entity_number) {
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "select %u", (unsigned)entity_number);
}

static void CL_WowSendAttack(DWORD entity_number) {
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "attack %u", (unsigned)entity_number);
}

/* LMB up: if mouse didn't move much, treat as a click → select target under cursor. */
static void CL_WowLmbUp(void) {
    DWORD entnum;

    wow_input.left_mouse = false;
    wow_input.lmb_down = false;
    if (!CL_GameplayInputReady()) {
        return;
    }
    if (CL_WowMouseMovedPast(&mouse.origin, &wow_input.lmb_down_pos)) {
        return; /* was a drag, not a click */
    }
    if (CL_MouseOverGameplayUI()) {
        return;
    }
    if (re.TraceEntity(&cl.viewDef, mouse.origin.x, mouse.origin.y, &entnum)) {
        CL_WowSendSelect(entnum);
    } else {
        CL_WowSendSelect(0); /* deselect */
    }
}

/* RMB up: if mouse didn't move much, treat as a click → context interact. */
static void CL_WowRmbUp(void) {
    DWORD entnum;

    wow_input.right_mouse = false;
    wow_input.rmb_dragging = false;
    SDL_SetRelativeMouseMode(SDL_FALSE);
    if (!CL_GameplayInputReady()) {
        return;
    }
    if (CL_WowMouseMovedPast(&mouse.origin, &wow_input.rmb_down_pos)) {
        return; /* was a camera drag, not a click */
    }
    if (CL_MouseOverGameplayUI()) {
        return;
    }
    /* Context interact: attack hostile entity under cursor. */
    if (re.TraceEntity(&cl.viewDef, mouse.origin.x, mouse.origin.y, &entnum)) {
        CL_WowSendAttack(entnum);
    }
}

static void IN_WowLeftDown(void) {
    wow_input.left_mouse = true;
    wow_input.lmb_down = true;
    wow_input.lmb_down_pos = mouse.origin;
}

static void IN_WowLeftUp(void) {
    CL_WowLmbUp();
}

static void IN_AttackDown(void) {
    /* Bound to +attack key — legacy path, treat same as LMB down. */
    wow_input.left_mouse = true;
    wow_input.lmb_down = true;
    wow_input.lmb_down_pos = mouse.origin;
}

static void IN_AttackUp(void) {
    CL_WowLmbUp();
}

static void IN_LookDown(void) {
    wow_input.right_mouse = true;
    wow_input.rmb_down_pos = mouse.origin;
    wow_input.rmb_dragging = true;
    CL_WowInitInputState();
    SDL_SetRelativeMouseMode(SDL_TRUE);
}

static void IN_LookUp(void) {
    CL_WowRmbUp();
}

static void IN_ForwardDown(void) {
    wow_input.move_forward = true;
}

static void IN_ForwardUp(void) {
    wow_input.move_forward = false;
}

static void IN_BackDown(void) {
    wow_input.move_back = true;
}

static void IN_BackUp(void) {
    wow_input.move_back = false;
}

static void IN_MoveLeftDown(void) {
    wow_input.move_left = true;
}

static void IN_MoveLeftUp(void) {
    wow_input.move_left = false;
}

static void IN_MoveRightDown(void) {
    wow_input.move_right = true;
}

static void IN_MoveRightUp(void) {
    wow_input.move_right = false;
}

void CL_InputModeInit(void) {
    Cmd_AddCommand("+wowleft", IN_WowLeftDown);
    Cmd_AddCommand("-wowleft", IN_WowLeftUp);
    Cmd_AddCommand("+attack", IN_AttackDown);
    Cmd_AddCommand("-attack", IN_AttackUp);
    Cmd_AddCommand("+wowattack", IN_AttackDown);
    Cmd_AddCommand("-wowattack", IN_AttackUp);
    Cmd_AddCommand("+look", IN_LookDown);
    Cmd_AddCommand("-look", IN_LookUp);
    Cmd_AddCommand("+forward", IN_ForwardDown);
    Cmd_AddCommand("-forward", IN_ForwardUp);
    Cmd_AddCommand("+back", IN_BackDown);
    Cmd_AddCommand("-back", IN_BackUp);
    Cmd_AddCommand("+moveleft", IN_MoveLeftDown);
    Cmd_AddCommand("-moveleft", IN_MoveLeftUp);
    Cmd_AddCommand("+moveright", IN_MoveRightDown);
    Cmd_AddCommand("-moveright", IN_MoveRightUp);
}

void CL_InputModeSetGameplay(void) {
    cl.viewDef.camerastate[0].zfar = 16000;
    cl.viewDef.camerastate[0].znear = 1;
    cl.viewDef.camerastate[1].zfar = 16000;
    cl.viewDef.camerastate[1].znear = 1;
}

void CL_InputModeMouseButton(SDL_MouseButtonEvent const *button, BOOL down) {
    (void)button;
    (void)down;
}

void CL_InputModeMouseMotion(SDL_MouseMotionEvent const *motion) {
    DWORD entnum;

    if (!motion) {
        return;
    }
    /* Camera rotation while RMB held. */
    if (wow_input.right_mouse) {
        wow_input.yaw -= motion->xrel * WOW_MOUSE_TURN_SPEED;
        wow_input.pitch = CL_WowClamp(wow_input.pitch - motion->yrel * WOW_MOUSE_TURN_SPEED,
                                      WOW_CAMERA_MIN_PITCH,
                                      WOW_CAMERA_MAX_PITCH);
    }
    /* Hover detection: trace entity under cursor every motion. */
    if (!CL_GameplayInputReady() || CL_MouseOverGameplayUI()) {
        if (cl.hover_entity != 0) {
            cl.hover_entity = 0;
            SDL_SetCursor(wow_input.cursor_arrow);
        }
        return;
    }
    if (re.TraceEntity(&cl.viewDef, (float)motion->x, (float)motion->y, &entnum)) {
        cl.hover_entity = entnum;
    } else {
        cl.hover_entity = 0;
    }
    /* Update context cursor based on hovered entity. */
    if (cl.hover_entity != wow_input.last_hover_entity) {
        wow_input.last_hover_entity = cl.hover_entity;
        if (cl.hover_entity == 0) {
            SDL_SetCursor(wow_input.cursor_arrow);
        } else {
            /* Find the render entity to check RF_HOSTILE. */
            BOOL hostile = false;
            FOR_LOOP(i, cl.viewDef.num_entities) {
                if (cl.viewDef.entities[i].number == cl.hover_entity) {
                    hostile = (cl.viewDef.entities[i].flags & RF_HOSTILE) != 0;
                    break;
                }
            }
            SDL_SetCursor(hostile ? wow_input.cursor_crosshair : wow_input.cursor_hand);
        }
    }
}

BOOL CL_InputModeMouseWheel(SDL_MouseWheelEvent const *wheel) {
    if (!wheel) {
        return false;
    }
    wow_input.distance = CL_WowClamp(wow_input.distance - wheel->y * 1.0f,
                                     WOW_CAMERA_MIN_DISTANCE,
                                     WOW_CAMERA_MAX_DISTANCE);
    return true;
}

void CL_InputModeFrame(void) {
    DWORD now;
    FLOAT dt;
    DWORD flags = 0;

    CL_WowInitInputState();
    if (cls.key_dest != key_game || cls.state != ca_active) {
        return;
    }

    now = SDL_GetTicks();
    dt = (FLOAT)(now - wow_input.last_time) / 1000.0f;
    if (dt < 0.0f || dt > 0.25f) {
        dt = 0.0f;
    }
    wow_input.last_time = now;

    if (wow_input.move_forward || (wow_input.left_mouse && wow_input.right_mouse)) {
        flags |= WOW_MOVE_FORWARD;
    }
    if (wow_input.move_back) {
        flags |= WOW_MOVE_BACK;
    }
    /* A/D always strafe (modern WoW). */
    if (wow_input.move_left) {
        flags |= WOW_MOVE_LEFT;
    }
    if (wow_input.move_right) {
        flags |= WOW_MOVE_RIGHT;
    }

    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message,
              "move %u %.3f %.3f %.3f",
              (unsigned)flags,
              (double)wow_input.yaw,
              (double)wow_input.pitch,
              (double)wow_input.distance);
}

/* No minimap or control groups in the WoW input mode. */
BOOL CL_TryMinimapClick(float x, float y) { (void)x; (void)y; return false; }
void CL_EndMinimapDrag(void) {}
BOOL CL_HandleGameKey(int sym, Uint16 mod) { (void)sym; (void)mod; return false; }
#endif
