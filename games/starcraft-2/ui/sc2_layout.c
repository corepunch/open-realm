/*
 * sc2_layout.c — SC2 .SC2Layout implementation (single-header trigger).
 *
 * Pulls in the full parser from stb_sc2layout.h.  This file exists so the
 * ui-sc2 unity build and test binary pick up the implementation automatically.
 */
#define STB_SC2LAYOUT_IMPLEMENTATION
#include "sc2_layout.h"
