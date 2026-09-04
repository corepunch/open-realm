#ifndef cl_input_local_h
#define cl_input_local_h

#include "client.h"

#include <SDL2/SDL.h>

BOOL CL_MouseOverGameplayUI(void);
BOOL CL_GameplayInputReady(void);
void CL_SetCameraPosition(VECTOR2 position);

void CL_InputModeInit(void);
void CL_InputModeSetGameplay(void);
void CL_InputModeResetMap(void);
void CL_InputModeMouseButton(SDL_MouseButtonEvent const *button, BOOL down);
void CL_InputModeMouseMotion(SDL_MouseMotionEvent const *motion);
BOOL CL_InputModeMouseWheel(SDL_MouseWheelEvent const *wheel);
void CL_InputModeFrame(void);
/* +select/-select. Return true to skip the generic RTS box/point path. */
BOOL CL_InputModeSelectDown(void);
BOOL CL_InputModeSelectUp(void);

/* Minimap click-to-move-camera. Returns true if the click was on the minimap
 * (and the camera was recentered). No-op / false outside RTS input mode. */
BOOL CL_TryMinimapClick(float x, float y);
void CL_UpdateMinimapDrag(float x, float y);
void CL_EndMinimapDrag(void);
BOOL CL_MinimapKeyEvent(int key, BOOL repeat);

#endif
