#include "client.h"
#include "cl_input_local.h"

#define CL_MINIMAP_PING_COUNT 16 // markers; bounds simultaneous transient minimap attention effects
#define CL_MINIMAP_RECENT_COUNT 8 // positions; bounds Warcraft-style recent-alert Space recall
#define CL_MINIMAP_PACKET_SIZE 17 // bytes; fixed svc_minimap_ping payload size used for bounds validation

typedef struct {
    bool active;
    VECTOR2 position;
    COLOR32 color;
    uint32_t start_time, end_time;
    uint32_t flags;
} minimapPing_t;

static bool minimap_drag_active;
static minimapPing_t minimap_pings[CL_MINIMAP_PING_COUNT];
static VECTOR2 minimap_recent[CL_MINIMAP_RECENT_COUNT];
static uint32_t minimap_recent_count, minimap_recent_cursor;

/* Keep each predicted XYZ sample terrain-relative before replacing its XY, including unacknowledged snapshots. */
void CL_PredictCameraPosition(VECTOR2 pos) {
    bool terrain = re.CameraUsesTerrainHeight();
    float height = terrain ? re.GetHeightAtPoint(pos.x, pos.y) : 0.0f;
    FOR_LOOP(i, 2) {
        LPVECTOR3 org = &cl.viewDef.camerastate[i].origin;
        /* Changing XY alone paired the previous terrain Z with the new location and caused acknowledgment jumps. */
        if (terrain) org->z += height - re.GetHeightAtPoint(org->x, org->y);
        org->x = pos.x; org->y = pos.y;
    }
}

/* Apply one camera position through local prediction and the authoritative client message. */
void CL_SetCameraPosition(VECTOR2 position) {
    position = CL_ClampCameraPosition(position);
    CL_PredictCameraPosition(position);
    cl.camera_prediction.active = true;
    cl.camera_prediction.origin = position;
    cl.camera_prediction.focus_ms = cl.time;
    MSG_WriteByte(&cls.netchan.message, clc_input);
    MSG_WriteInput(&cls.netchan.message, &(INPUTCMD){ .action = BZ_INPUT_FOCUS, .focus = position });
}

void CL_ClearMinimap(void) {
    minimap_drag_active = false;
    memset(minimap_pings, 0, sizeof(minimap_pings));
    memset(minimap_recent, 0, sizeof(minimap_recent));
    minimap_recent_count = minimap_recent_cursor = 0;
}

/* Keep newest alert positions first so Space traversal is deterministic. */
static void CL_RememberMinimapPosition(LPCVECTOR2 position) {
    uint32_t move = MIN(minimap_recent_count, CL_MINIMAP_RECENT_COUNT - 1);
    if (move) memmove(&minimap_recent[1], minimap_recent, move * sizeof(*minimap_recent));
    minimap_recent[0] = *position;
    minimap_recent_count = MIN(minimap_recent_count + 1, CL_MINIMAP_RECENT_COUNT);
    minimap_recent_cursor = 0;
}

/* Decode the fixed transient marker packet directly into client presentation state. */
void CL_ParseMinimapPing(LPSIZEBUF msg) {
    minimapPing_t ping = { .active = true, .start_time = cl.time };
    uint32_t slot = CL_MINIMAP_PING_COUNT, oldest = 0, oldest_age = 0;
    float duration;

    if (!msg || msg->cursize - msg->readcount < CL_MINIMAP_PACKET_SIZE) {
        fprintf(stderr, "CL_ParseMinimapPing: truncated payload\n");
        if (msg) msg->readcount = msg->cursize;
        return;
    }
    ping.position.x = MSG_ReadFloat(msg); ping.position.y = MSG_ReadFloat(msg);
    duration = MSG_ReadFloat(msg);
    ping.color = MAKE(COLOR32, MSG_ReadByte(msg), MSG_ReadByte(msg), MSG_ReadByte(msg), MSG_ReadByte(msg));
    ping.flags = (uint32_t)MSG_ReadByte(msg);
    if (!isfinite(ping.position.x) || !isfinite(ping.position.y) || !isfinite(duration) || duration <= 0.0f ||
        duration > MINIMAP_PING_DURATION_MAX) {
        fprintf(stderr, "CL_ParseMinimapPing: invalid duration=%.3f\n", duration);
        return;
    }
    ping.end_time = cl.time + (uint32_t)MAX(1.0f, duration * 1000.0f);
    FOR_LOOP(i, CL_MINIMAP_PING_COUNT) {
        uint32_t age;
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
        rect_t marker;
        float pulse;
        if (!ping->active) continue;
        if ((int32_t)(cl.time - ping->end_time) >= 0) { ping->active = false; continue; }
        if (!re.WorldToMinimap(&ping->position, &screen)) continue;
        if (cl.minimap_model) {
            re.DrawSprite(&MAKE(drawSprite_t, .model = cl.minimap_model, .anim = "Stand", .x = screen.x, .y = screen.y, .id = &cl.minimap_model));
            continue;
        }
        pulse = 3.0f + (float)((cl.time - ping->start_time) % 500) / 250.0f;
        marker = MAKE(rect_t, screen.x - pulse, screen.y - 1.0f, pulse * 2.0f, 2.0f);
        re.DrawFill(&marker, ping->color);
        marker = MAKE(rect_t, screen.x - 1.0f, screen.y - pulse, 2.0f, pulse * 2.0f);
        re.DrawFill(&marker, ping->color);
        if (ping->flags & MINIMAP_PING_EXTRA_EFFECTS) {
            marker = MAKE(rect_t, screen.x - pulse - 2.0f, screen.y - pulse - 2.0f, pulse * 2.0f + 4.0f, 1.0f);
            re.DrawFill(&marker, ping->color);
        }
    }
}

/* Load the authored alert model named by the Quake-style CS_MINIMAP slot. */
void CL_UpdateMinimapModel(void) {
    cstring_t name = cl.configstrings[CS_MINIMAP];

    SAFE_DELETE(cl.minimap_model, re.ReleaseModel);
    if (!name[0]) return;
    cl.minimap_model = re.LoadModel(name);
    if (!cl.minimap_model)
        fprintf(stderr, "CL_UpdateMinimapModel: failed to load %s\n", name);
}

/* Draw the server-authored minimap frame and all transient attention markers. */
void CL_LayoutDrawMinimap(LPCUIFRAME frame, rect_t const * screen) {
    /* bool is a byte and truncated bit 15 to zero, selecting the unloaded gameplay texture. */
    bool preview = frame->flagsvalue & UIFLAG_MINIMAP_PREVIEW;
    re.DrawMinimap(screen, preview ? frame->text : NULL);
    if (!preview) CL_DrawMinimapPings();
}

/* Left-click (or click-drag) on the minimap recenters the camera there. */
bool CL_TryMinimapClick(float x, float y) {
    VECTOR2 world;
    /* TraceMinimap is mandatory; its result reports whether a minimap was hit. */
    if (!CL_GameplayInputReady() || !re.TraceMinimap(x, y, &world)) return false;
    minimap_drag_active = true;
    CL_SetCameraPosition(world);
    return true;
}

void CL_UpdateMinimapDrag(float x, float y) {
    VECTOR2 world;
    if (!minimap_drag_active || !re.TraceMinimap(x, y, &world)) return;
    CL_SetCameraPosition(world);
}

void CL_EndMinimapDrag(void) { minimap_drag_active = false; }

/* Space cycles newest-first through remembered attention markers. */
bool CL_MinimapKeyEvent(int key, bool repeat) {
    if (key != SDLK_SPACE || !minimap_recent_count || !CL_GameplayInputReady() || CL_WindowModalActive()) return false;
    if (repeat) return true;
    if (minimap_recent_cursor >= minimap_recent_count) minimap_recent_cursor = 0;
    CL_SetCameraPosition(minimap_recent[minimap_recent_cursor++]);
    if (minimap_recent_cursor >= minimap_recent_count) minimap_recent_cursor = 0;
    return true;
}

#ifdef BZ_TESTS
uint32_t CL_MinimapPingCount(void) {
    uint32_t count = 0;
    FOR_LOOP(i, CL_MINIMAP_PING_COUNT) count += minimap_pings[i].active ? 1 : 0;
    return count;
}
uint32_t CL_MinimapRecentCount(void) { return minimap_recent_count; }
#endif
