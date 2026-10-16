#ifndef cl_input_local_h
#define cl_input_local_h

#include "client.h"

#include <SDL2/SDL.h>

/* Cmd_TokenizeString's 64-token limit includes the command name. */
#define CL_SELECTION_CANDIDATE_LIMIT (MAX_SELECTED_ENTITIES - 1u)

bool CL_MouseOverGameplayUI(void);
bool CL_GameplayInputReady(void);
void CL_SetCameraPosition(vec2_t position);

void CL_ResetInput(void);
uint32_t CL_SelectionLimit(void);
void CL_ApplySelection(uint32_t const *ids, uint32_t n);
void CL_ApplySelectionCandidates(uint32_t const *ids, uint32_t n);

/* Nine significant decimal digits preserve every finite binary32 coordinate
 * across the existing string-command transport. */
static inline void CL_SendWorldPointCommand(cstring_t command, float x, float y) {
    SDL_Keymod const mods = SDL_GetModState();
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s %.9g %.9g%s%s", command, (double)x, (double)y,
              (mods & (KMOD_LSHIFT | KMOD_RSHIFT)) ? " queue" : "",
              (mods & (KMOD_LALT | KMOD_RALT)) ? " alt" : "");
}

static inline void CL_SendPointCommand(float x, float y) {
    CL_SendWorldPointCommand("point", x, y);
}

/* Minimap click-to-move-camera. Returns true if the click was on the minimap
 * (and the camera was recentered). No-op / false without a minimap. */
bool CL_TryMinimapClick(float x, float y);
void CL_UpdateMinimapDrag(float x, float y);
void CL_EndMinimapDrag(void);
bool CL_MinimapKeyEvent(int key, bool repeat);

#endif
