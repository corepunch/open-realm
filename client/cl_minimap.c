#include "client.h"
#include "cl_input_local.h"

#define CL_MINIMAP_PING_COUNT 16 // markers; bounds simultaneous transient minimap attention effects
#define CL_MINIMAP_RECENT_COUNT 8 // positions; bounds Warcraft-style recent-alert Space recall
#define CL_MINIMAP_PACKET_SIZE 17 // bytes; fixed svc_minimap_ping payload size used for bounds validation

typedef struct {
    BOOL active;
    VECTOR2 position;
    COLOR32 color;
    DWORD start_time, end_time;
    DWORD flags;
} minimapPing_t;

static BOOL minimap_drag_active;
static minimapPing_t minimap_pings[CL_MINIMAP_PING_COUNT];
static VECTOR2 minimap_recent[CL_MINIMAP_RECENT_COUNT];
static DWORD minimap_recent_count, minimap_recent_cursor;

/* Apply one camera position through local prediction and the authoritative client message. */
void CL_SetCameraPosition(VECTOR2 position) {
    position = CL_ClampCameraPosition(position);
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

void CL_ClearMinimap(void) {
    minimap_drag_active = false;
    memset(minimap_pings, 0, sizeof(minimap_pings));
    memset(minimap_recent, 0, sizeof(minimap_recent));
    minimap_recent_count = minimap_recent_cursor = 0;
}

/* Keep newest alert positions first so Space traversal is deterministic. */
static void CL_RememberMinimapPosition(LPCVECTOR2 position) {
    DWORD move = MIN(minimap_recent_count, CL_MINIMAP_RECENT_COUNT - 1);
    if (move) memmove(&minimap_recent[1], minimap_recent, move * sizeof(*minimap_recent));
    minimap_recent[0] = *position;
    minimap_recent_count = MIN(minimap_recent_count + 1, CL_MINIMAP_RECENT_COUNT);
    minimap_recent_cursor = 0;
}

/* Decode the fixed transient marker packet directly into client presentation state. */
void CL_ParseMinimapPing(LPSIZEBUF msg) {
    minimapPing_t ping = { .active = true, .start_time = cl.time };
    DWORD slot = CL_MINIMAP_PING_COUNT, oldest = 0, oldest_age = 0;
    FLOAT duration;

    if (!msg || msg->cursize - msg->readcount < CL_MINIMAP_PACKET_SIZE) {
        fprintf(stderr, "CL_ParseMinimapPing: truncated payload\n");
        if (msg) msg->readcount = msg->cursize;
        return;
    }
    ping.position.x = MSG_ReadFloat(msg); ping.position.y = MSG_ReadFloat(msg);
    duration = MSG_ReadFloat(msg);
    ping.color = MAKE(COLOR32, MSG_ReadByte(msg), MSG_ReadByte(msg), MSG_ReadByte(msg), MSG_ReadByte(msg));
    ping.flags = (DWORD)MSG_ReadByte(msg);
    if (!isfinite(ping.position.x) || !isfinite(ping.position.y) || !isfinite(duration) || duration <= 0.0f ||
        duration > MINIMAP_PING_DURATION_MAX) {
        fprintf(stderr, "CL_ParseMinimapPing: invalid duration=%.3f\n", duration);
        return;
    }
    ping.end_time = cl.time + (DWORD)MAX(1.0f, duration * 1000.0f);
    FOR_LOOP(i, CL_MINIMAP_PING_COUNT) {
        DWORD age;
        if (!minimap_pings[i].active) { slot = i; break; }
        age = cl.time - minimap_pings[i].start_time;
        if (slot == CL_MINIMAP_PING_COUNT && age >= oldest_age) { oldest = i; oldest_age = age; }
    }
    if (slot == CL_MINIMAP_PING_COUNT) slot = oldest;
    minimap_pings[slot] = ping;
    if (ping.flags & MINIMAP_PING_REMEMBER) CL_RememberMinimapPosition(&ping.position);
}

/* Draw authored models when supplied; otherwise use a generic colored attention marker. */
static void CL_DrawMinimapPings(void) {
    FOR_LOOP(i, CL_MINIMAP_PING_COUNT) {
        minimapPing_t *ping = &minimap_pings[i];
        VECTOR2 screen;
        RECT marker;
        FLOAT pulse;
        if (!ping->active) continue;
        if ((LONG)(cl.time - ping->end_time) >= 0) { ping->active = false; continue; }
        if (!re.WorldToMinimap(&ping->position, &screen)) continue;
        if (cl.minimap_model) {
            re.DrawSprite(cl.minimap_model, "Stand", screen.x, screen.y);
            continue;
        }
        pulse = 3.0f + (FLOAT)((cl.time - ping->start_time) % 500) / 250.0f;
        marker = MAKE(RECT, screen.x - pulse, screen.y - 1.0f, pulse * 2.0f, 2.0f);
        re.DrawFill(&marker, ping->color);
        marker = MAKE(RECT, screen.x - 1.0f, screen.y - pulse, 2.0f, pulse * 2.0f);
        re.DrawFill(&marker, ping->color);
        if (ping->flags & MINIMAP_PING_EXTRA_EFFECTS) {
            marker = MAKE(RECT, screen.x - pulse - 2.0f, screen.y - pulse - 2.0f, pulse * 2.0f + 4.0f, 1.0f);
            re.DrawFill(&marker, ping->color);
        }
    }
}

/* Load the authored alert model named by the Quake-style CS_MINIMAP slot. */
void CL_UpdateMinimapModel(void) {
    LPCSTR name = cl.configstrings[CS_MINIMAP];

    SAFE_DELETE(cl.minimap_model, re.ReleaseModel);
    if (!name[0]) return;
    cl.minimap_model = re.LoadModel(name);
    if (!cl.minimap_model)
        fprintf(stderr, "CL_UpdateMinimapModel: failed to load %s\n", name);
}

/* Draw the server-authored minimap frame and all transient attention markers. */
void CL_LayoutDrawMinimap(LPCUIFRAME frame, LPCRECT screen) {
    (void)frame;
    re.DrawMinimap(screen);
    CL_DrawMinimapPings();
}

/* Left-click (or click-drag) on the minimap recenters the camera there. */
BOOL CL_TryMinimapClick(float x, float y) {
    VECTOR2 world;
    if (!CL_GameplayInputReady() || !re.TraceMinimap || !re.TraceMinimap(x, y, &world)) return false;
    minimap_drag_active = true;
    CL_SetCameraPosition(world);
    return true;
}

void CL_UpdateMinimapDrag(float x, float y) {
    VECTOR2 world;
    if (!minimap_drag_active || !re.TraceMinimap || !re.TraceMinimap(x, y, &world)) return;
    CL_SetCameraPosition(world);
}

void CL_EndMinimapDrag(void) { minimap_drag_active = false; }

/* Space cycles newest-first through remembered attention markers. */
BOOL CL_MinimapKeyEvent(int key, BOOL repeat) {
    if (key != SDLK_SPACE || !minimap_recent_count || !CL_GameplayInputReady() || CL_WindowModalActive()) return false;
    if (repeat) return true;
    if (minimap_recent_cursor >= minimap_recent_count) minimap_recent_cursor = 0;
    CL_SetCameraPosition(minimap_recent[minimap_recent_cursor++]);
    if (minimap_recent_cursor >= minimap_recent_count) minimap_recent_cursor = 0;
    return true;
}

#ifdef BZ_TESTS
DWORD CL_MinimapPingCount(void) {
    DWORD count = 0;
    FOR_LOOP(i, CL_MINIMAP_PING_COUNT) count += minimap_pings[i].active ? 1 : 0;
    return count;
}
DWORD CL_MinimapRecentCount(void) { return minimap_recent_count; }
#endif
