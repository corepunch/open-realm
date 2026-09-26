#ifndef cl_input_local_h
#define cl_input_local_h

#include "client.h"

#include <SDL2/SDL.h>

bool CL_MouseOverGameplayUI(void);
bool CL_GameplayInputReady(void);
void CL_SetCameraPosition(vec2_t position);

void CL_ResetInput(void);
uint32_t CL_SelectionLimit(void);
void CL_ApplySelection(uint32_t const *ids, uint32_t n);

/* Minimap click-to-move-camera. Returns true if the click was on the minimap
 * (and the camera was recentered). No-op / false without a minimap. */
bool CL_TryMinimapClick(float x, float y);
void CL_UpdateMinimapDrag(float x, float y);
void CL_EndMinimapDrag(void);
bool CL_MinimapKeyEvent(int key, bool repeat);

#endif
