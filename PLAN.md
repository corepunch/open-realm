# WoW Mouse Interaction Implementation Plan

## Goal

Replace the current "LMB = attack nearest, ignore mouse location" behavior with correct WoW mouse interaction:
- **LMB click**: Select/target entity under cursor
- **RMB click**: Context interact (attack hostile, talk to NPC, loot)
- **RMB held**: Camera rotation (already works)
- **Both held**: Move forward (already works)
- **A/D**: Always strafe
- **Hover**: Entity highlighting + context cursor

## Status: COMPLETE

All 6 phases implemented. 421/421 tests pass.

## Phase 1: Server-Side Hostile Flag

Add `RF_HOSTILE` render flag so clients know which entities can be targeted.

- `common/shared.h`: Add `RF_HOSTILE` (bit 13) and `RF_HOVERED` (bit 14) to renderfx enum; add `selected_entity` field to `playerState_s`
- `games/world-of-warcraft/game/g_wow_local.h`: Add `BOOL hostile` to `wowEntityLocal_t`
- `games/world-of-warcraft/game/m_creature.c`: Set `local->hostile = true` and `ent->s.renderfx |= RF_HOSTILE` in `Wow_MonsterStart()`
- `games/world-of-warcraft/game/g_wow.c`: Set `local->hostile = false` for player in `Wow_InitPlayer()`

## Phase 2: WoW "select" Command on Server

Add select/deselect command handling in the WoW game module.

- `games/world-of-warcraft/game/g_wow.c`: Add `"select"` case to `Wow_ClientCommand()` — parse entity number, store in `client->ps.selected_entity`

## Phase 3: Client-Side Mouse Actions

Rewrite WoW mouse handlers for correct LMB/RMB behavior.

- `client/cl_input_wow.c`:
  - LMB: Click to select (trace entity under cursor, send `"select %d"`)
  - RMB up (quick click): Context interact (trace entity, send `"attack %d"` for hostile)
  - RMB held: Camera rotation (keep existing)
  - Both held: Move forward (keep existing)
  - A/D: Always strafe (removed turn-when-no-rmb branch)
  - Hover: Track entity under cursor on mouse motion
- `client/client.h`: Add `hover_entity` field to `client_state`

## Phase 4: Cursor Context Icons

Context-sensitive cursor based on what's under the mouse.

- `client/cl_input_wow.c`: On mouse motion, determine context and call `SDL_SetCursor()`:
  - Hostile entity → crosshair cursor
  - Friendly NPC → hand cursor
  - Nothing → arrow cursor

## Phase 5: Hover Health Bars

Show health bars for hovered entities.

- `client/tr_public.h`: Add `hover_entity` to `viewDef_t`
- `client/cl_view.c`: Pass `cl.hover_entity` to `cl.viewDef.hover_entity`
- `renderer/r_ents.c`: Extended `R_DrawHealthBars()` to show bars for hovered entities

## Phase 6: Hover Visual Feedback

Highlight entities under cursor.

- `renderer/r_ents.c`: `R_RenderHoverHighlight()` draws a subtle circle (red for hostile, green for friendly) under hovered entities

## File Change Summary

| File | Changes |
|------|---------|
| `common/shared.h` | `RF_HOSTILE`, `RF_HOVERED`, `selected_entity` in playerState |
| `games/world-of-warcraft/game/g_wow_local.h` | `hostile` in `wowEntityLocal_t` |
| `games/world-of-warcraft/game/g_wow.c` | `"select"` command, player `hostile=false` |
| `games/world-of-warcraft/game/m_creature.c` | Set `hostile=true`, `RF_HOSTILE` |
| `client/cl_input_wow.c` | Rewrite mouse handlers, hover tracking, A/D strafe, context cursors |
| `client/client.h` | `hover_entity` field |
| `client/tr_public.h` | `hover_entity` in viewDef_t |
| `client/cl_view.c` | Pass hover_entity to viewDef |
| `renderer/r_ents.c` | Hover highlight, extended health bars |
