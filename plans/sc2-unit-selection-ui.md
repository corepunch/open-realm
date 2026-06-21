# SC2 Unit Selection → UI Flow

## Goal

Implement the server-authored HUD update path for SC2: when the player selects a
unit, the server reads unit command data from GameData XML catalogs, builds
command-button / portrait / info-panel proxy frames, and sends them to the
client via `svc_layout` — the same architecture WC3 uses in `g_ui_stubs.c`.

## Reference files (read, not modified)

| File | Role |
|------|------|
| `games/warcraft-3/game/g_ui_stubs.c` | `Get_Commands_f`, `Get_Portrait_f`, `UI_WriteCommandButtonFrame`, `UI_WriteSingleInfo` |
| `games/warcraft-3/game/g_unit_ui.c` | `G_GetCommandButtons`, `G_BuildCommandButton` |
| `games/warcraft-3/game/g_commands.c` | `CMD_Select`, `G_SelectEntity`, `G_GetMainSelectedUnit` |
| `common/shared.h` | `LAYER_*`, `uiFrame_t`, `FT_COMMANDBUTTON`, `svc_layout` |
| `server/game.h` | `gameCommandButton_t`, `gameImport_t.Write`, `gameImport_t.unicast` |

## SC2 Game Data (inside MPQ archives)

| Archive path | XML file | Contains |
|---|---|---|
| `Mods/Core.SC2Mod/Base.SC2Data` | `GameData/AbilData.xml` | Ability definitions (`CAbilAttack`, `CAbilMove`, `CAbilStop`, …) with `CmdButtonArray` |
| `Mods/Core.SC2Mod/Base.SC2Data` | `GameData/ButtonData.xml` | Button faces (`CButton`) — `Icon`, `Hotkey`, tooltips |
| `Mods/Core.SC2Mod/Base.SC2Data` | `GameData/UnitData.xml` | Units (`CUnit`) — `AbilArray`, `CardLayouts`/`LayoutButtons`, stats |
| `Mods/Core.SC2Mod/enUS.SC2Data` | `LocalizedData/GameStrings.txt` | Display names: `Unit/Name/<id>=…`, `Button/Tooltip/<id>=…` |
| `Mods/Core.SC2Mod/enUS.SC2Data` | `LocalizedData/GameHotkeys.txt` | Hotkey bindings: `Button/Hotkey/<buttonId>=<key>` |

CardLayout example (from UnitData.xml):
```xml
<LayoutButtons Face="Move"  Type="AbilCmd" AbilCmd="move,Move"    Row="0" Column="0"/>
<LayoutButtons Face="Stop"  Type="AbilCmd" AbilCmd="stop,Stop"    Row="0" Column="1"/>
<LayoutButtons Face="Attack" Type="AbilCmd" AbilCmd="attack,Execute" Row="0" Column="4"/>
```

## Steps

### Phase 1 — Data layer (`g_sc2_data.c`, new file)

**File:** `games/starcraft-2/game/g_sc2_data.c`

1. Define lookup tables (reuse existing `sc2XmlField_t` pattern from `sc2_map.c`):
   ```c
   typedef struct { DWORD id; char name[64]; char icon[256]; char hotkey; } sc2ButtonInfo_t;
   typedef struct { DWORD id; char name[128]; DWORD button_id; int row, col; char abilcmd[128]; } sc2UnitCmd_t;
   typedef struct { DWORD id; char name[128]; DWORD cmds[16]; int num_cmds; } sc2UnitInfo_t;
   ```

2. `SC2_DataInit(void)` — called from `SC2_Init()`:
   - Parse `GameData/ButtonData.xml` via libxml2 + `sc2_read_catalog_xml()`:
     iterate `CButton` nodes, read `id`, `Icon`, `Hotkey`, `EditorCategories` →
     populate `sc2ButtonInfo_t sc2_buttons[]` array.
   - Parse `GameData/UnitData.xml`:
     iterate `CUnit` nodes, read `id`, `AbilArray`, `CardLayouts`/`LayoutButtons`
     (`Face`, `Type`, `AbilCmd`, `Row`, `Column`) → populate `sc2UnitInfo_t sc2_units[]`.
   - Parse `LocalizedData/GameStrings.txt` and `GameHotkeys.txt` into string
     lookup tables for tooltip/hotkey text.
   - Use `strdup` / `gi.MemAlloc` for strings (MPQ data is transient).

3. `SC2_DataShutdown(void)` — free allocations.

4. Lookup helpers:
   - `SC2_ButtonInfo(DWORD button_id) → sc2ButtonInfo_t *`
   - `SC2_UnitInfo(DWORD class_id) → sc2UnitInfo_t *`
   - `SC2_UnitTooltip(DWORD class_id) → LPCSTR` (from GameStrings)

### Phase 2 — Command button builder (`g_sc2_unit_ui.c`, new file)

**File:** `games/starcraft-2/game/g_sc2_unit_ui.c`

Modeled after WC3 `g_unit_ui.c` but reads SC2 XML catalogs instead of SLK sheets.

1. `SC2_GetCommandButtons(LPEDICT ent, gameCommandButton_t *buttons, BYTE max) → BYTE`:
   - Look up `sc2UnitInfo_t` by `ent->class_id`.
   - For each command in `unit->cmds[]`:
     - Look up `sc2ButtonInfo_t` by `button_id`.
     - Fill `gameCommandButton_t`:
       - `art` = icon path from button info
       - `tooltip` = localized name from GameStrings
       - `command` = abilcmd string (e.g. `"move,Move"`)
       - `hotkey` = from button info
       - `x` = column, `y` = row (from LayoutButtons)
       - `active` = 1
   - Return count.

2. `SC2_GetMainSelectedUnit(LPGAMECLIENT client) → LPEDICT`:
   - Iterate edicts where `ent->selected & (1 << client->ps.number)`.
   - Return first match.

3. `SC2_SelectedUnits(LPGAMECLIENT client, LPEDICT *out, DWORD max) → DWORD`:
   - Fill `out[]` with all selected units for this player.

### Phase 3 — HUD frame writers (`g_sc2_ui_stubs.c`, new file)

**File:** `games/starcraft-2/game/g_sc2_ui_stubs.c`

Modeled after WC3 `g_ui_stubs.c` `Get_Commands_f` / `Get_Portrait_f`.

1. `SC2_UI_ClearLayer(LPEDICT ent, DWORD layer)`:
   ```c
   gi.Write(PF_BYTE, &(LONG){svc_layout});
   gi.Write(PF_BYTE, &(LONG){layer});
   gi.Write(PF_LONG, &(LONG){0});
   gi.Write(PF_SHORT, &(LONG){0});
   gi.unicast(ent);
   ```

2. `SC2_UI_WriteCommandButton(gameCommandButton_t *button)`:
   - Build a `uiFrame_t` with `type = FT_COMMANDBUTTON`.
   - Set `tex.index = gi.ImageIndex(button->art)`.
   - Set `stat = button->active`.
   - Set `tooltip = button->tooltip`.
   - Set `onclick = "button <abilcmd>"`.
   - Compute rect from `UI_CommandButtonRect(x, y)`.
   - Call `gi.Write(PF_UIFRAME, &frame)`.

3. `SC2_Get_Commands_f(LPEDICT ent)`:
   - Get main selected unit via `SC2_GetMainSelectedUnit()`.
   - `SC2_UI_ClearLayer(ent, LAYER_COMMANDBAR)`.
   - If no unit, return.
   - `SC2_GetCommandButtons(selected, buttons, 12)`.
   - Write `svc_layout` header for `LAYER_COMMANDBAR`.
   - For each button, call `SC2_UI_WriteCommandButton()`.
   - Write terminator `(0, 0)`.
   - `gi.unicast(ent)`.

4. `SC2_Get_Portrait_f(LPEDICT ent)`:
   - Send `LAYER_PORTRAIT` with a single portrait frame (unit model icon).
   - Send `LAYER_INFOPANEL` with unit name / stats text.
   - `gi.unicast(ent)`.

### Phase 4 — Wire selection to UI (`g_sc2.c` changes)

In `SC2_Select()`, after setting the selection bitmask:

```c
/* After the selection loop, call UI refreshes like WC3 does */
SC2_Get_Commands_f(clent);
SC2_Get_Portrait_f(clent);
```

Also add selection helpers to `g_sc2_local.h`:
```c
LPEDICT SC2_GetMainSelectedUnit(LPGAMECLIENT client);
DWORD   SC2_SelectedUnits(LPGAMECLIENT client, LPEDICT *out, DWORD max);
```

### Phase 5 — Client stubs (`ui_main.c` changes)

Implement the stub functions so the client receives layout layers:

```c
static void SC2_UI_SetLayoutLayer(DWORD layer, HANDLE data) {
    /* Layout layers are stored by the client in cl.layout[layer].
     * The UI library doesn't need to interpret them — the client
     * renderer draws them directly via CL_LayoutDrawOverlays(). */
    (void)layer; (void)data;
}

static void SC2_UI_ClearLayoutLayer(DWORD layer) {
    /* Same — client owns the data. */
    (void)layer;
}
```

These are already correct as stubs; the client stores layout blobs in
`cl.layout[layer]` and renders them without UI library involvement.
No changes needed here — WC3 works the same way (stubs in ui_main.c,
client does all layout rendering).

### Phase 6 — Build integration

Add new files to the Makefile SC2 game target:

```makefile
SC2_GAME_SRCS += games/starcraft-2/game/g_sc2_data.c
SC2_GAME_SRCS += games/starcraft-2/game/g_sc2_unit_ui.c
SC2_GAME_SRCS += games/starcraft-2/game/g_sc2_ui_stubs.c
```

## File summary

| File | Action | Purpose |
|------|--------|---------|
| `games/starcraft-2/game/g_sc2_data.c` | **New** | Parse AbilData/UnitData/ButtonData XML catalogs |
| `games/starcraft-2/game/g_sc2_unit_ui.c` | **New** | Command button builder, selection helpers |
| `games/starcraft-2/game/g_sc2_ui_stubs.c` | **New** | `Get_Commands_f`, `Get_Portrait_f`, proxy frame writers |
| `games/starcraft-2/game/g_sc2_local.h` | **Edit** | Add selection helper declarations |
| `games/starcraft-2/game/g_sc2.c` | **Edit** | Call `SC2_Get_Commands_f` / `SC2_Get_Portrait_f` from `SC2_Select()` |
| `games/starcraft-2/ui/ui_main.c` | **No change** | Stubs already correct — client renders layout layers |
| `Makefile` | **Edit** | Add new .c files to SC2 game target |
