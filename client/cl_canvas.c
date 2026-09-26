/*
 * cl_canvas.c — UI canvas: the layer between SDL window geometry and everything drawn in authored UI units.
 *
 * Owns the scene the renderer projects, the HUD root the layout solver anchors to, the pointer mapping, and
 * the presentation class the game authors chrome for (docs/architecture/ui-canvas.md).  Window changes
 * re-resolve the live canvas immediately, so every frame lays out against the current window and nothing is
 * remounted during an interactive resize.  Only the class the server authors for waits until the window has
 * held still for CL_CANVAS_SETTLE_MS, so a drag across the 4:3 boundary costs one chrome re-author, not one
 * per mouse step.
 */
#include "client.h"
#include "common/ui_canvas.h"

/* Longer than any interactive drag step, shorter than a noticeable delay before extension chrome appears. */
#define CL_CANVAS_SETTLE_MS 250 // milliseconds; a window size must hold this long before the class is committed

static struct {
    uiCanvas_t live;         // canvas for the current window; drives layout, projection and input every frame
    size2_t stable;        // window size seen at the last frame boundary; a difference restarts the settle timer
    UICANVASCLASS settled; // class committed after the window settled; what the server should author for
    UICANVASCLASS sent;    // class last written to the current connection; UI_CANVAS_CLASS_COUNT before begin
    uint32_t changed;         // frame time at which the current window size was first observed; settle timer origin
} canvas;

static cstring_t const canvas_policy_names[] = { "stretch", "expand", "expand-center" };

static void CL_CanvasPush(void) { re.SetUIScene(&canvas.live.scene); }

/* Re-resolve against a window size and hand the scene to the renderer.  A zero-sized (minimized or not yet
 * mapped) window keeps the last canvas so layout, input and the settle timer never see a degenerate scene. */
static void CL_CanvasResolve(size2_t window) {
    if (!window.width || !window.height) return;
    canvas.live = UI_ResolveCanvas(window, canvas.live.policy);
    CL_CanvasPush();
}

/* The mounted archives decide the policy (widescreen console chrome authored or not); an edition switch
 * changes which archives are visible, so CL_RebuildMenu resolves again. */
void CL_CanvasResolvePolicy(void) {
    UICANVASPOLICY policy = CL_GameCanvasPolicy();
    if (policy != canvas.live.policy) fprintf(stderr, "UI canvas: %s policy\n", canvas_policy_names[policy]);
    canvas.live.policy = policy;
    canvas.live = UI_ResolveCanvas(canvas.live.window, canvas.live.policy);
    CL_CanvasPush();
}

void CL_CanvasInit(void) {
    memset(&canvas, 0, sizeof(canvas));
    canvas.sent = UI_CANVAS_CLASS_COUNT;
    canvas.live.policy = CL_GameCanvasPolicy();
    canvas.live = UI_ResolveCanvas(re.GetWindowSize(), canvas.live.policy);
    canvas.stable = canvas.live.window;
    canvas.settled = canvas.live.chrome;
    fprintf(stderr, "UI canvas: %s policy\n", canvas_policy_names[canvas.live.policy]);
    CL_CanvasPush();
}

/* SDL window events arrive inside CL_Input, before the same poll pass delivers the mouse events that hit-test
 * against the canvas, so the resolve happens at once rather than at the frame boundary. */
void CL_CanvasWindowChanged(void) { CL_CanvasResolve(re.GetWindowSize()); }

/* Once per client frame: catch size changes that arrived without an event (vid_apply, platforms that resize
 * silently), advance the settle timer, and tell the live server when the settled class differs from the one
 * it authored for.  The timer starts at the first frame boundary that observes a new size, so a size that
 * flips and flips back inside one poll pass never restarts it.  Cmd_ForwardToServer rejects pre-active
 * states, so the commit waits for ca_active; the begin handshake commits through CL_CanvasWriteChrome. */
void CL_CanvasFrame(uint32_t now) {
    char command[32];
    CL_CanvasResolve(re.GetWindowSize());
    if (canvas.live.window.width != canvas.stable.width || canvas.live.window.height != canvas.stable.height) {
        canvas.stable = canvas.live.window;
        canvas.changed = now;
    }
    if (canvas.settled != canvas.live.chrome && now - canvas.changed >= CL_CANVAS_SETTLE_MS)
        canvas.settled = canvas.live.chrome;
    if (cls.state != ca_active || canvas.sent == canvas.settled) return;
    canvas.sent = canvas.settled;
    snprintf(command, sizeof(command), "ui_canvas %u", (unsigned)canvas.sent);
    Cmd_ForwardToServer(command);
}

/* Begin authors the whole HUD from scratch, so the live window is committed immediately instead of waiting for
 * a settle that may still be running from a fullscreen transition at startup. */
void CL_CanvasWriteChrome(void) {
    char command[32];
    canvas.settled = canvas.sent = canvas.live.chrome;
    snprintf(command, sizeof(command), "ui_canvas %u", (unsigned)canvas.sent);
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    MSG_WriteString(&cls.netchan.message, command);
}

uiCanvas_t const * CL_Canvas(void) { return &canvas.live; }

/* Presentation class the server should currently author for; tests and diagnostics read it, gameplay never does. */
UICANVASCLASS CL_CanvasSettledChrome(void) { return canvas.settled; }
