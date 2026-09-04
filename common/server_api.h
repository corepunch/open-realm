#ifndef COMMON_SERVER_API_H
#define COMMON_SERVER_API_H

#include "shared.h"

void SV_Init(void);
void SV_Frame(DWORD msec);
void SV_InitGameProgs(void);
void SV_Map(LPCSTR mapFilename);
BOOL SV_GetSaveMap(LPCSTR name, LPSTR map, DWORD map_size);
BOOL SV_IsActive(void);

#endif
