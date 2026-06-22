# WoW Local Character Storage

## Goal

When the player clicks "Accept" on the character creation screen, serialize the
new character to `share/characters.xml`.  On the character selection screen
("Enter Realm"), read that file and display the list.

## Background

The Lua side is already correct — it calls `CreateCharacter(name)` on Accept,
`GetNumCharacters()` and `GetCharacterInfo(index)` to populate the list.  All
three are no-op stubs in C.  There is no file-write path in the engine today.

`share/` is the writable local directory (configs live there).  `FS_ReadFile`
reads from MPQ archives; there is no `FS_WriteFile` yet.

## Character XML Schema

```xml
<Characters>
  <Character name="Arthas" race="1" sex="1" class="2" appearance="439042" />
  <Character name="Jaina"  race="1" sex="2" class="8" appearance="118529" />
</Characters>
```

`appearance` is the packed DWORD from `Wow_PackAppearance()`.

## Implementation Plan

### 1. Add `FS_WriteFile` to `uiImport_t`  (`client/ui.h`)

```c
void (*FS_WriteFile)(LPCSTR path, const void *data, int size);
```

### 2. Wire it in `cl_main.c`

Add `CL_UI_WriteFile` — resolves the path relative to the CWD (same as
`share/` config files) and writes with `fopen`/`fwrite`.

### 3. Add `wow_charlist` to `ui_dbc.c`

A static array of up to 10 character entries:

```c
typedef struct {
    char   name[64];
    DWORD  race_id;
    DWORD  sex_id;
    DWORD  class_id;
    DWORD  appearance;
} wowCharEntry_t;
```

Load `share/characters.xml` once at startup (lazy, on first
`GetNumCharacters` call).  Parse with a tiny hand-rolled XML reader — no
external dependency.

### 4. Implement `UIWow_LuaCreateCharacter`  (`ui_lua.c`)

- Read name from Lua arg 1
- Pack appearance from `wow_charcreate` fields via `Wow_PackAppearance()`
- Append `<Character …/>` element to `share/characters.xml` (create file if
  missing)
- Append to the in-memory `wow_charlist` too
- Fire `CharacterCreateResult("OKAY")` Lua event so the UI proceeds

### 5. Implement `UIWow_LuaGetNumCharacters`  (`ui_lua.c`)

- Trigger lazy load of `wow_charlist`
- Return count

### 6. Implement `UIWow_LuaCharacterInfo`  (`ui_lua.c`)

WoW's `GetCharacterInfo(index)` returns 10 values:
`name, level, class, race, sex, zone, guild, status, className, raceFileName`

We return: `name, 1, className, raceName, sex, "", "", "", className, raceFile`

### 7. Invalidate cache on `CreateCharacter`

Set a `charlist_loaded = false` flag so the next `GetNumCharacters` re-reads
the file (handles multiple sessions cleanly).

## Files Changed

| File | Change |
|------|--------|
| `client/ui.h` | Add `FS_WriteFile` to `uiImport_t` |
| `client/cl_main.c` | Add `CL_UI_WriteFile`, wire into `uiImport_t` |
| `games/world-of-warcraft/ui/ui_dbc.c` | `wow_charlist`, XML load/save helpers, `UIWow_LoadCharList` |
| `games/world-of-warcraft/ui/ui_lua.c` | Implement `CreateCharacter`, `GetNumCharacters`, `GetCharacterInfo` |

## Out of Scope

- Character deletion (`DeleteCharacter`) — stub remains
- Server-side character sync
- More than 10 characters per account
