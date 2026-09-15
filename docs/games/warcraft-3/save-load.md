# Warcraft III Save/Load

## Contract

The WC3 game module owns save/load. `GetGameAPI()` exposes `SaveGame` and `LoadGame` callbacks through `server/game.h`; the JASS `SaveGame` and `LoadGame` natives use the same callbacks and resolve names through `gi.SavePath`.

`WriteGame()` writes the current game state to a versioned binary file. The file contains:

- `W3SV` magic, format version 24, canonical map path, `sizeof(edict_t)`, entity count, client count, script identity, and native-handle registry counts;
- level frame/time, authoritative Warcraft time-of-day state, map-global camera bounds, and started/script-started flags;
- each client `GAMECLIENT` state, including its `PLAYER` state, JASS settings, runtime removed/result-presentation state, researched tech, text storage, camera values, messages, and HUD caches;
- each camera target as an entity index;
- the quest and quest-item graph's strings and status flags;
- the fixed point-order waypoint edict ring and its circular allocation cursor;
- one used flag per entity slot and a raw `edict_t` block for used slots;
- group membership, trigger enabled state, timer state, weather-effect registry state, unread gameplay events, and a semantic JASS VM snapshot;
- a `W3OK` commit footer and FNV-1a checksum over the complete preceding payload.

`WriteGame()` removes the destination when any record or footer write fails. `ReadGame()` validates the commit footer, checksum, format, script identity, and quest/group/trigger/timer/event registry counts before mutating clients or entities. A truncated or rejected partial write therefore cannot become a loadable artifact or clear the live world. A header mismatch names the failing field and prints saved versus live counts; do not treat a generic `header mismatch` line as complete.
Quest objects and items are restored in place so the running JASS VM's light handles keep their object identity. Events use `MAX_EVENTS` fixed slots, quests use `MAX_QUESTS` slots, and each quest owns `MAX_QUESTITEMS` item slots; `inuse` marks lifecycle state without moving live pointers during removal. Loading rejects a quest or item count mismatch instead of leaving those handles dangling. Loading completely reloads the saved map first, then applies state.

The versioned layout retains the authoritative `level.timeofday` record and game-state event condition fields (`state`, `limitop`, `limitval`) and the client removal/pending-result fields used by victory/defeat presentation. Quest and event records are written by the recursive field schema. Counted descriptors write the count followed by the array prefix. Since version 13 the dynamic JASS group registry is written immediately after the level-field stream: every handle ordinal through `level.num_groups` writes `ggroup_t.inuse`, `num_units`, and that many `F_EDICT` indexes. Inactive holes remain serialized so higher live handle ordinals do not shift. Version 14 adds `GAMEEVENT.value`, the scalar callback payload used by research events, and pairs it with JASS snapshot format 3 so a sleeping callback preserves `JASSCONTEXT.eventValue` across save/load. Version 17 adds `GAMEEVENT.point` / `has_point` and pairs it with JASS snapshot format 4 so point-target spell response context survives unread event queues and yielded trigger coroutines.

The version 3 layout expands the fixed-size `GAMECLIENT` cinematic camera state with target Z offset, near/far clipping planes, and target-controller orientation inheritance. Version 2 saves are rejected because the raw client record layout changed; this prevents older saves from being misread with shifted fields.

The version 4 layout packs `PLAYER.cinematic_portrait`, `team`, `color`, and `race` as consecutive `BYTE`s (one `NFT_LONG` on the wire), drops `PLAYER.camera_bounds` (the rectangle lives on `level`), and writes `level.camera_bounds` with the other level clocks. Version 3 saves are rejected because both the raw `GAMECLIENT` record and the level stream changed.

Version 10 extends the authoritative `level.timeofday` record with the Warsmash-style temporary/false clock (`hour`, `minute`,
remaining simulation ticks, active, initialized), so a loaded save cannot silently resume the canonical day/night cycle while a saved
Moonstone-style override should still be active. Version 11 adds per-slot JASS group lifecycle state so destroyed group slots can be safely recycled and restored. Version 12 expands the raw `GAMECLIENT` snapshot with semantic Warcraft music state (map/default selection, current source, start/seek position, pause state, and music/thematic volumes). Version 13 replaces the fixed inline group array with a growable stable-pointer registry and serializes that registry separately, so v12 and earlier saves are rejected rather than being interpreted with the wrong level layout. Version 15 accompanies the natural-creep sleep edict fields. Version 16 adds `level.environment_fog.active` and `.defaults` so scripted distance mist and the `ResetTerrainFog` target survive save/load.
Version 17 accompanies the WC3 `edict_t` vertex-colour fields used by `SetUnitVertexColor`; the changed edict size and format version reject older records instead of interpreting shifted state.
Version 17 also adds dedicated per-unit `abilitycooldowns[]` records so active ability cooldown windows survive save/load without sharing the timed buff/status array. Spell point response context in queued events and the matching JASS snapshot state are part of the same version. Version 18 adds the per-unit Polymorph restoration record (source ability/buff, selected form, original model/scale/movement speed, and active flag), paired with the already-persistent timed buff so an active morph restores correctly after load.
Version 19 adds the ability-owned per-unit Raven Form takeoff state (`fly_height`, rise start/duration, and rise phase) so a save taken during the authored post-morph ascent cannot restore with an invalid altitude transition. `raven_fields` explicitly serializes those four scalar fields; no new edict pointers are introduced.
Version 20 adds race-specific construction lifecycle state and an explicit `F_EDICT` fixup for `construction.worker`; v19 saves are rejected rather than interpreting the expanded raw edict record.
Version 21 adds `mineoverlay.parent` and `acolyte_mine.mine` `F_EDICT` fixups plus overlay income timing/index and Acolyte slot/generation state. The original `Agld` edict remains the finite resource owner for Haunted/Entangled overlays, so those relationships must survive load rather than being reconstructed from proximity.
Version 22 adds neutral-shop stock state: global item/unit slot capacities plus each shop edict's initialized item-stock entries, current counts, and absolute start/replenish deadlines. `stock_fields` bounds `item_count` by `MAX_SHOP_STOCK`; the deadlines use the restored simulation clock, so partially elapsed restock timers resume without rebasing. The expanded `edict_t` layout and format version reject older records instead of interpreting shifted state.
Version 23 adds the fixed TimerDialog registry. Each live slot persists its timer association, title/colour presentation state, and client visibility mask; non-null JASS `timerdialog` values snapshot as stable slot indexes. Layout payloads remain transient, so load invalidates the timer-dialog HUD cache and republishes restored state on the next frame. See [Timer Dialogs And Mission Countdowns](timer-dialogs.md).
Version 24 adds the fixed leaderboard registry, ordered item/style/color state, per-player leaderboard assignment indexes, per-client display masks, and stable JASS `leaderboard` handle IDs. Leaderboard layout payloads remain transient and are republished after load. See [Leaderboards And Counted Objective HUDs](leaderboards.md).

Groups use reusable stable ordinals in a growable pointer table: `level.num_groups` is the high-water mark while `level.group_capacity` is transient allocation capacity. Each `ggroup_t` is separately allocated so growing the pointer table never moves a live handle. `DestroyGroup` releases an ordinal for later reuse; `GroupClear` only clears membership. Live JASS group handles serialize as stable ordinal indexes. See [JASS Groups](jass-groups.md).

Groups, timers, triggers, and event handlers may grow after `main()`. The header accepts a save that has *at least* as many of those objects as the freshly initialized map, then `RestoreRegistrySlots()` allocates the extras. A live count higher than the save still rejects. Quests remain an exact match because they are restored in place.

The current format is process-independent for entity relationships: `F_EDICT` fields and camera targets are written as entity indexes and resolved back to `g_edicts[index]` by `ReadGame()`. Before raw edict records replace the freshly loaded map baseline, `ReadGame()` clears the baseline spatial tree and then links each restored entity exactly once. Client pointers are restored from player slots, player names from inline JASS name storage, and map-player rows from the loaded map plus `PLAYER.number`. Malformed headers, truncated records, and entity indexes reject the load; client pointers are never read from the file as addresses.

Groups, triggers, timers, and events use deterministic handle ordinals. Group objects are dynamically allocated behind a growable pointer table; membership is stored as entity indexes and each serialized record persists `inuse` so destroyed holes remain distinguishable from live empty groups. Each trigger stores its disabled flag plus action/condition function names so a trigger created after `main()` still has its callbacks after load. Timers preserve their handler name, duration, remaining time, periodic/paused/running flags, and resume relative to the load time. Timer callbacks and timer-expire trigger actions enter the normal coroutine queue and retain `GetExpiredTimer()` context.

Weather effects use the same stable-slot rule. `level.weather_effects[MAX_WEATHER_EFFECTS]`, `next_weather_id`, each slot's rawcode, rectangle, enabled flag, and renderer handle ID are serialized as level state. A non-null JASS `weathereffect` snapshots as its fixed slot index, so globals keep pointer identity across load. `ReadGame()` restores the registry before the JASS snapshot and replays it to connected clients; reconnecting clients also receive the same full weather sync from `G_ClientBegin()`. See [Weather](weather.md).

Event handler registrations store type, subject entity index, trigger index, timer index, region, range, game-state ID, `limitop`, and limit value. The extra condition fields preserve `TriggerRegisterGameStateEvent` time-of-day registrations across save/load. The unread portion of the bounded gameplay event ring preserves event type, subject/source entity indexes, scalar value, optional point target, and the target registration ordinal. Consumed queue entries are not saved. Loads reject queue overflow and unresolved entity or registration IDs.


## JASS Snapshot

The embedded snapshot starts with `JSVM`, snapshot format version 4, a program-identity hash, mutable-global count, and sleeping-coroutine count. It stores:

- mutable scalar globals and sparse array entries;
- integer, real, boolean, string, code, null, and supported typed-handle values;
- shared-handle identity by stable native-domain ID;
- VM-owned handle identity and bounded payloads for sounds, camera setups, rects, locations, forces, game caches, regions, fog modifiers, and converted value objects;
- `boolexpr`, `conditionfunc`, and `filterfunc` handles by semantic JASS function name;
- sleeping coroutine frames as function/block token ordinals, locals, operand stack values, wake delay, and event context, including scalar and optional point spell response data.

The snapshot never writes parser pointers, dictionary links, refcount addresses, stack pointers, or `jmp_buf`. Code values and coroutine PCs resolve against the already-parsed program after the identity hash matches. Handles relocate through game-owned codecs for entities (`unit`, `widget`, `destructable`, `item`, `effect`), players, quests, quest items, events, triggers, groups, timers, timer dialogs, leaderboards, and weather effects. Safe VM-owned handles serialize their payload and snapshot-local identity so aliases remain aliases after load. Unsupported non-null handle types reject the save with a diagnostic instead of writing an address or silently dropping the value.

Handle encoding dispatches value handles, VM-owned payloads, and function handles before consulting the game host. A failed host lookup means null only for host-owned native domains, such as a removed unit; applying that rule to VM-owned handles would silently replace valid sounds, camera setups, rects, locations, forces, and game caches with null.

JASS sound handle payloads remain part of the VM snapshot, but one-shot presentation parameters currently maintained by the game host (`SetSoundVolume`, `SetSoundPosition`, and `AttachSoundToUnit`) are transient. `ReadGame()` clears that host-side table before reconstructed sound handles can reuse an old pointer value. Scripts that need those presentation parameters after restore must set them again before the next `StartSound`; continuous playback state is not yet serialized.

Point-order waypoints follow Quake II's body-queue/TRAIL pattern: 256 classless, collisionless, `SVF_NOCLIENT` edicts are reserved before map entities and recycled as a ring. Its base, count, and cursor live in serialized level state. Consequently `goalentity` and other waypoint references use the ordinary `F_EDICT` index relocation path; there is only one entity pointer address domain.

Saving is allowed only at a VM safe point. `jass_writesnapshot()` rejects a request while a synchronous JASS call or coroutine is actively executing. Yielded `TriggerSleepAction` coroutines are safe and resume from their saved semantic PC after load.

## Field Table

`games/warcraft-3/game/g_save.c` keeps the `field_t fields[]` table synchronized with `struct edict_s` in `g_local.h`. Fixed-size
`edict_t` and `GAMECLIENT` records are still copied as one block. Embedded non-pointer state such as `abilstatus[]` (including each
timed status's `timestamp` and `duration_ms`), `abilitycooldowns[]` (cooldown rawcode/start/end), and the inline WC3 animation-property strings (`animation_props` and
`animation_request`) therefore round-trip with that raw record and need no `field_t` entry. The adjacent `runtime_fields[]` and
`client_runtime_fields[]` tables describe the process-owned bytes that must be zeroed before that copy. This keeps the
common path memcpy-shaped while making pointer exceptions declarative rather than a hand-maintained assignment list.

`EDICTFIELD(x, type)` describes one scalar field with `array_size == 0`. `EDICTFIELD(x, type, count)` describes a contiguous array from the base offset; the serializer walks `count` elements using the field type's element size. For example, the six inventory pointers use `EDICTFIELD(inventory, F_EDICT, MAX_INVENTORY)` rather than six duplicate descriptors.

- Add every persistent `edict_t` entity pointer to `fields[]` as `F_EDICT`, including array elements and nested fields.
- Use `TFC(type, field, kind, capacity, count_field)` for a bounded typedef-backed array. The descriptor writes and restores `count_field` itself, then processes that many elements; do not map the count separately.
- Persistent movement defaults are part of that rule: Attack-Move/Patrol waypoints and `movement.follow_target` must be encoded as entity indexes rather than raw pointers.
- Do not add process-owned pointers such as path textures, metadata rows, or animations. `WriteEdict()` clears `FIELD_RUNTIME` pointers and `ReadEdict()` rebinds class metadata; spatial links are rebuilt with `gi.LinkEntity`.
- Edict C callbacks (`think`, `stand`, `birth`, `prethink`, `die`, `idle`, `move`, `run`, `attack`, `pain`) use `F_CFUNCTION`, not `F_IGNORE`. Add every production assignment to the append-only `save_cfunctions[]` roster in `g_save.c`; an unrostered pointer fails the save instead of writing an address.
- JASS `F_FUNCTION` remains name-string identity for timers and triggers. Do not overload it with C symbols.
- Add remaining process-owned edict or client pointers to the corresponding runtime-field table so the fixed record copy cannot write an address into the save file.
- When adding a new pointer or changing an existing edict field, update the table and the round-trip test together. A raw pointer omitted from the table can write an address into the save file.
- Keep the table sentinel `{ NULL, 0, 0, 0 }`; all serializer loops stop at `field->name == NULL`.

This follows the Quake 2 `g_save.c` pattern while avoiding Quake 2's old global pointer addresses and unbounded save stream.

## Native Usage

JASS save names are relative user-state names. Names containing `/` or `\\` are rejected. The engine's `FS_SavePath()` policy determines the writable directory.

```jass
call SaveGame("chapter-01")
call LoadGame("chapter-01", false)
```

`LoadGame` completely reloads the saved map before restoring state. The initialized map must have the same JASS program. `main()` recreates the baseline native registries; objects created later in gameplay are allocated from the save and filled from trigger/event/timer records. The native names resolve through `FS_SavePath()`.

## Quake 2 Lifecycle

`data/Quake-2-master` is the reference. WC3 cannot copy the files 1:1 (one map, embedded JASS instead of cross-level `game.ssv`), but the steps must stay in Q2 order.

| Q2 | This engine |
| --- | --- |
| `SV_Savegame_f` → `WriteServerFile` / `WriteGame` / `WriteLevel` | `save` → `ge->SaveGame` / `WriteGame` |
| `SV_Loadgame_f` → `ReadServerFile` / `ReadGame` | save header supplies the map path |
| `CL_Changing_f`: plaque + `ca_connected` | `CL_BeginLoadingMap` + `CL_RestartRefresh` |
| `SV_Map(..., loadgame)` → `SpawnEntities`, two `RunFrame`s, `SV_CreateBaseline` | `SV_Map` → `G_LoadMap` / `G_StartScripts` (`main()`) |
| `SV_CheckForSavegame`: `SV_ClearWorld` then `ReadLevel` | `SV_LoadGame` calls `ReadGame` immediately (`gi.ClearWorld` then edicts) |
| `SV_Map` ends with `reconnect`; already-connected client sends `new` | `SV_Map` sends loopback `client_connect`; do **not** `CL_Connect` |
| `CL_ParseServerData` → `CL_ClearState` zeros `refresh_prepped` and `image_precache` | `CL_RestartRefresh` zeros prepped flags plus `cl.pics` / `cl.fonts` / `cl.models` because same-map load never changes `CS_WORLD` |

Q2 `ReadLevel` states the contract we hit: SpawnEntities has already run the same way as at save time, the server has cleared world links, then edicts are overwritten and `linkentity` rebuilds the tree. Skipping `ClearWorld` left the baseline area lists pointing at the same edict addresses and hung in `SV_AreaEdicts_r`. Calling `CL_Connect` after that `client_connect` wiped the client netchan and raced the handshake — Q2 never does that on load.

`F_EDICT` / `F_CLIENT` still convert pointers to indexes only at the save boundary. Q2 `F_FUNCTION` stored a process-relative code offset. We split that job: JASS `F_FUNCTION` stores script names, and edict C callbacks use `F_CFUNCTION` (see [C Callbacks](#c-callbacks-f_cfunction)).

## JASS Handles Stay Pointers at Runtime

Do not replace JASS VM `HANDLE` / `LPCJASSFUNC` / `LPEDICT` fields with integers. Q2 keeps `edict_t *` and `think` pointers in memory and remaps them in `WriteField1` / `ReadField`. The VM should do the same.

- Host-owned natives (`unit`, `widget`, `item`, `player`, `quest`, `trigger`, `group`, `timer`, `timerdialog`, `event`, `weathereffect`) snapshot as stable ordinals through `G_SaveJassHandle` / `G_LoadJassHandle`.
- Stale host-owned handles are normalized to null at the save boundary, including handles retained by yielded coroutine/event contexts. Removed units can outlive their edict only as stale JASS pointers; they must not make an otherwise valid save fail. A missing host codec or snapshot I/O failure remains fatal.
- VM-owned payloads (sounds, rects, locations, forces, game caches) snapshot identity plus bytes.
- `code` / trigger actions snapshot as function names, the analog of Q2 `F_FUNCTION` without a relocated code segment.

Integer handle tables inside the interpreter would duplicate that field table, break light-handle aliasing, and still need a remap step on load. When a new pointer appears on `edict_t` or a native object, add a field/codec entry; do not change the VM's in-memory representation.

HUD FDF trees cache `CS_IMAGES` / `CS_FONTS` slots. Those tables die with `memset(&sv)` in `SV_Map`. `G_LoadMap` memsets the single `hud` accumulator and clears the FDF pool; serialize then re-`ImageIndex`es from names. See [HUD Media Lifetime](hud-media.md).

The format does not yet snapshot gameplay fog-of-war grids, bot runtime, alliances, stock state, or cinematic filter. Environmental distance-fog state is serialized since version 16. Client message storage is part of `GAMECLIENT`, but transient presentation lifetimes are not reconstructed. Edict C callbacks persist through `F_CFUNCTION` (see [C Callbacks](#c-callbacks-f_cfunction)); the active `umove_t` is restored by `F_MMOVE` relocation (see [Active Behavior](#active-behavior-f_mmove)). Menu callbacks are code pointers and are reset on load; restoring an active targeting/build submenu requires a semantic menu-state enum rather than raw function addresses. There is no backwards-compatible reader for v12 or earlier saves. Version 9 packs C-callback roster indexes into the edict blob that version 8 zeroed and rebound from class data. Version 10 adds the fixed weather-effect registry, weather handle IDs, the `weathereffect` JASS handle codec, and false-time state to the level stream; after load the authoritative weather set is replayed to connected clients. Version 11 adds per-slot JASS group lifecycle state so destroyed group slots can be safely recycled and restored. Version 12 expands `GAMECLIENT` with semantic music state; the client decoder itself is not serialized, so music resumes from the last JASS-defined start/seek position rather than an exact continuously advancing decoder head. Version 13 serializes the dynamically sized JASS group registry outside the fixed level-field schema while preserving group-handle ordinals.

The checksum and header preflight protect normal partial/corrupt-file and wrong-map failures before mutation. Record-level semantic validation later in the stream is not fully transactional; do not treat save files as untrusted input until native records are decoded into temporary state before commit.

## Simulation Clock Continuity

`level.time` is the only clock the game may read. Game code calls `G_Time()` (an inline read of
`level.time`); it must not call `gi.GetTime()` directly, because spell-rank parameters named `level`
shadow the global in several skill functions and would silently pick up the wrong symbol.

The server owns the simulation clock, following Quake II's `sv.time` model. `SV_Map` resets the
per-level server state, so `ReadGame` restores the saved time through the generic
`gi.SetGameTime(time)` import before the next server frame. `sv.framenum` is deliberately not
restored: it indexes the snapshot delta ring (`client->frames[sv.framenum & UPDATE_MASK]`) and is
process state, so rewinding it would desynchronise a connected client. `G_RunFrame` then reads the
authoritative server clock:

```c
level.time = gi.GetTime();
```

Every persisted absolute deadline lives in this clock: `edict_s.spawn_time`, `edict_s.freetime`,
`edict_s.heatmap2_time`, `heroabilitystatus_t.timestamp`, client `camera.start_time` /
`message.end_time` / `cinematic_end_time`, and `level.cinefilter`. A save taken at `level.time = 20800`
must restore the server clock to `20800`; otherwise every deadline would sit ~20.7 s in the future and
units would stall waiting for cooldowns that already elapsed. Symptom seen in the field:
everything stands frozen while one script-controlled unit walks off to a stale waypoint goal.

Do not "fix" this by re-basing individual subsystems at load (the older per-timer
`started = gi.GetTime(); timeout = remaining` rebase). Restoring the server tick covers every
deadline; a per-subsystem rebase silently misses the edict and client-presentation deadlines.

JASS timers deliberately hold **no** clock-absolute state. `gtimer_s` stores `duration` plus a
`remaining` countdown that `G_RunTimers` decrements by `FRAMETIME` each frame, so a timer reloads
with exactly the time it had left and needs no rebase at all. Prefer this shape for any new
persisted deadline.

Regression test: `wc3_save.load_restores_server_clock_onto_saved_time` in
`games/warcraft-3/game/tests/t_game.c`.

## Active Behavior (`F_MMOVE`)

`edict_s.currentmove` is the running `umove_t` state machine. `monster_think` returns immediately
when it is NULL, so a unit that loads without it does not move, stand, animate, or advance its order
queue — the whole world stands frozen while scripted units walk off to stale goals.

Quake II solves this with `F_MMOVE` and we copy it exactly: every `umove_t` is a file-scope static in
`libgame`, so the pointer is saved as a signed byte offset from an anchor symbol in the same data
segment (`mmove_reloc` there, `umove_reloc` here) and re-added on load.

We add one guard Q2 does not have. A save written by a different build would decode to a wild
pointer, so the unused upper half of the 8-byte pointer field carries an FNV hash of the move's
animation name; `ReadField` range-checks the offset and compares the hash before dereferencing, and
fails the load loudly on a mismatch rather than resuming with a corrupt behavior.

`animation` stays a runtime field: `M_MoveFrame` already rebuilds it via `unit_setmove` when it finds
a NULL animation, so `currentmove` is the only pointer that has to survive.

## C Callbacks (`F_CFUNCTION`)

`F_FUNCTION` is JASS-only: timers and triggers store `jass_functionname` / `jass_functionbyname` strings.
Edict C callbacks cannot use that path; `monster_think`, `G_EffectThink`, and `blight_mine_think` are
C symbols, not JASS names.

`F_CFUNCTION` is the Q2 analog for those pointers. `WriteField1` looks the pointer up in
`save_cfunctions[]` and packs a 1-based roster index plus an FNV name hash into the 8-byte pointer
slot of the memcpy'd edict blob (same packing shape as `F_MMOVE`). `ReadField` restores the function
from that index after the hash matches. NULL stays 0/0. An unrostered pointer fails `WriteGame` with
`C callback %p is not in the save roster`; a bad index or hash fails the load instead of installing
a wild pointer.

The roster is append-only because the index is in the file. Production assignments retained by version 10:
`monster_think`, `blight_mine_think`, `G_FreeEdict`, `G_EffectThink`, `G_EffectValidateTarget`,
`blizzard_think`, `flame_strike_tick`, `siphon_mana_think`, `unit_stand`/`unit_birth`/`unit_die`,
and `tree_stand`/`tree_birth`/`tree_pain`/`tree_die`. `idle`/`move`/`run`/`attack` have no
production assignments yet; they still go through `F_CFUNCTION` so a later assignment must be
rostered.

`ReadEdict()` rebinds SLK table rows with `G_BindEntityData` but does **not** call
`G_BindEntityRuntime`. Class defaults would clobber a saved `blight_mine_think`, `G_EffectThink`,
or a valid NULL `think` (finished effect). `G_BindEntityRuntime` remains the spawn/test helper that
installs class-owned unit/destructable callbacks when those pointers have not been assigned yet.

Regression tests: `wc3_save.round_trip_entity_c_callbacks` and `wc3_save.rejects_unknown_c_callback`.

## In-Game ESC Menu Named Saves

`hud/hud_menu.c` binds Blizzard's `EscMenuSaveGamePanel.fdf` and uses the normal writable save directory for single-player named
saves. The transient-window bridge now carries two pieces of client-owned control state needed by that panel:

- `FT_EDITBOX` / `FT_GLUEEDITBOX` serialize as `uiEditBox_t` with a stable FDF control ID and maximum length; the client retains the
  typed text/cursor while the modal is open. Retained edit state is resolved only against the currently prepared transient-window
  layout because frame numbers are local to each layout; the same numeric frame in the gameplay HUD must not inherit the save name.
- `FT_LISTBOX` retains selected-row and scroll state locally. A row may be encoded as `display\thidden-value`; drawing stops at the
  tab while command placeholder expansion returns the hidden value.

Button commands may contain `{ControlName}` placeholders. `client/cl_window.c` expands them at activation from the current edit/list
state and escapes `"` / `\` before forwarding the command. The WC3 panel therefore uses:

```text
Esc -> Save Game -> type name -> menu_save_named "{SaveGameFileEditBox}"
Esc -> Load Game -> select row -> menu_load_named "{<transient list control name>}"
```

The list payload may name an edit target. Selecting a saved-game row copies its hidden basename into `SaveGameFileEditBox`, allowing
the same Save action to overwrite that slot or save an amended name.

The gameplay Save/Load dialog serializes Blizzard's `EscMenuSaveGamePanel` directly. Its `FileListFrame` is an intentionally empty
placeholder populated by retail code, so OpenRealm instantiates the authored `MapListBox` backdrop and scrollbar there and adds a
native `FT_LISTBOX` state child constrained by its anchors. The panel is hosted inside the shared Esc-menu backdrop, and the FDF remains
the geometry source of truth. The transient client owns list selection, scrolling, and edit state while the
modal is open; gameplay code only fills data, visibility, enable state, and click handlers.

New saves are prefilled with a filesystem/command-safe local timestamp (`YYYY-MM-DD_HH-MM-SS`). The value remains an ordinary
editable transient edit box, so the player can replace or amend it before saving.

Each time Save Game opens, the authored `SaveGameFileEditBox`
the edit box is initialized from local wall-clock time as `YYYY-MM-DD_HH-MM-SS`; the value is only a default and remains editable before
submission. The timestamp avoids characters rejected by the save-path validator.

`FS_ListSaves()` enumerates `.sav` basenames under the directory derived by `FS_SavePath()` and returns a double-NUL list ordered by
filesystem modification time, newest first. Equal modification times fall back to a case-insensitive basename sort for deterministic
ordering. This keeps recently written or overwritten saves at the top even when the player replaces the timestamp with a custom name.
The list is exposed to game modules through `gi.ListSaves`. The WC3 menu only publishes entries for which `G_GetSaveMap()` can read a map identity.
The basename `quick` is rendered as `Quick Save`, preserving the F6/console quick slot alongside named files.

Menu-entered names are trimmed, optional `.sav` is removed, length is capped to `CMDARG_LEN - 1`, and path/control characters are
rejected. `Quick Save` normalizes back to `quick`. Saving an existing basename currently overwrites it immediately. Delete removes the
selected basename through the filesystem import and replaces the same unique Save window, preserving modal ownership while refreshing
the rows. The authored overwrite-confirm panel remains disabled.

Loading remains a session boundary. `menu_load_named` calls `G_RequestLoadGameNamed()`, which queues `MenuAction("load", name)`. The
client resolves the saved map and calls `SV_LoadGame()` on the following frame, after the gameplay-window callback has returned. Do
not call `ReadGame()` directly from an in-game button callback.

The client forwards a `close_window_command` suffix before releasing the modal window, so a save request can execute while
`client_s.modal_flags` still contains the ESC-menu owner. Treat `modal_flags` and `quest_dialog_open` as `FIELD_RUNTIME`: they describe
live client-window ownership, not simulation state. Persisting them can reload a game as paused even though no transient window exists.
The serializer round-trip test asserts that both fields clear on load.

## Console Usage

The server registers Quake 2-style `save` and `load` commands. Save names are relative to the writable save directory and cannot contain path separators:

```sh
build/bin/openwarcraft3 -data "data/Warcraft III" +map "Maps/(2)Rivercross.w3m" +wait +save chapter-01
```

Open the in-game console with the backtick/tilde key and enter `save chapter-01` or `load chapter-01`. For command-line save diagnostics, place `+wait` between the map and `+save` commands so map initialization and `main()` have created the authoritative native registries first. A load invocation can omit `+map`:

```sh
build/bin/openwarcraft3 -data "data/Warcraft III" +load chapter-01
```

`FS_SavePath()` resolves saves below the same per-user directory as config: `$XDG_DATA_HOME/warcraft-3/saves/<name>.sav` on Unix when `XDG_DATA_HOME` is set to an absolute path, otherwise `~/.local/share/warcraft-3/saves/<name>.sav`; Windows uses `%APPDATA%/warcraft-3/saves/<name>.sav`. macOS follows the Unix XDG/fallback rule rather than `~/Library/Application Support`. If no writable per-user data directory is available, config and saves fall back to `share/warcraft-3/config/` and `share/warcraft-3/saves/`. The save filename may not contain `/` or `\`.

The ESC Save Game and Load Game panels issue the named `menu_save_named` / `menu_load_named` commands described above. The shipped
config still binds `F6` to `save quick`; `F9` remains the quest log shortcut. Console `save <name>` / `load <name>` continue to use the
same files, so a named ESC-menu save is also loadable from the console and vice versa.

## Verification

Run the serializer round trip against both ROC and TFT test environments:

```sh
make test-wc3-engine WC3_PATTERN='wc3_save.*'
```

The tests cover entity/client fields and pointer fixups, waypoint targets, quests, mutable globals and sparse arrays, code values, native and VM-owned handle aliases/payloads, function handles, group membership, trigger state, paused/periodic timers, direct and trigger timer callbacks, `GetExpiredTimer`, sleeping-coroutine resume, checksum rejection, script mismatch, native-registry mismatch, and triggers/events created after `main()`. Corruption and preflight tests assert that rejection leaves representative live entity state unchanged. ROC and TFT are both executed by the target.

A listen-server Human02 save/load is not covered by dedicated `+test`. Confirm it with a command-buffer replay that reaches `ca_active` after `load quick`:

```sh
# wait-lines omitted; keep enough frames for main() plus a few seconds of play
build/bin/openwarcraft3 -data "data/Warcraft III" -roc -com_fast_forward \
  +set vid_hidden 1 +set r_norefresh 1 \
  +map "Maps/Campaign/Human02.w3m" +exec /tmp/save-load.cfg +com_frame_limit 250
```

The load succeeded when the log contains `WC3 LoadGame: restored` and a later `CL_SendBegin` for the same map, and does **not** contain `header mismatch` or `restoring map baseline`. Repeat with `-tft`.



### Save/Load menu diagnostics

For targeted diagnostics without changing normal behavior, use:

```text
+set wc3_save_menu_debug 1
+set ui_window_debug 1
```

`wc3_save_menu_debug 1` logs each `.sav` basename discovered for the in-game
Save/Load dialog, the resolved path/map header, filtering reasons, the final
list payload, timestamp default, and Save/Load button enable state. Level `2`
additionally logs the authored list control, font metrics, and dimensions.

`ui_window_debug 1` logs the transient client payload for edit boxes and
listboxes, including control IDs, screen rectangles, font resources, item
height, selected row, and the text received over the wire. Level `2` additionally
logs live `SDL_TEXTINPUT` updates and the current client-side edit value/cursor.

For the two current presentation failures, capture both sets of lines beginning
with `WC3_SAVE_MENU` and `UI_WINDOW_DEBUG` after opening Save, typing a few
characters, then opening Load.

### Authored Save/Load presentation

The gameplay Save/Load dialog uses `EscMenuSaveGamePanel.fdf` without C-side geometry overrides. Its save-name edit field receives a
fresh editable date/time default whenever Save Game opens.

Transient edit-box text is client-local while the modal is open. Rendering of the authored edit-box text child must therefore read the
live client edit value, not only the server-supplied initial `STRING` text, so edits remain visible before submission.

The authored selectable list supports wheel, arrow, track, and thumb-drag scrolling by saved-game row.

## Natural creep sleep state

Save format version 15 accompanies the `edict_t` layout addition for natural creep sleep. The mutable `can_sleep` and
`sleeping` booleans are ordinary persistent edict scalars. A sleeping unit's `currentmove` continues to use the existing
`F_MMOVE` relocation path, so the static creep-sleep move is restored through the same ASLR-safe contract as other unit moves.
See [Neutral Creep Sleep](creep-sleep.md).

## Environmental distance fog

Save format version 16 adds the active WC3 environmental terrain-fog state and its `[DefaultZFog]` reset target to the level field schema. `ReadGame()` republishes the restored active state through `CS_SCENE_FOG` after the map reload and level-state restore, preventing the client from keeping the freshly initialized map fog instead of the saved scripted value. See [Environmental Terrain Fog](environmental-fog.md).

Channel cast serials, saved origins, and owner/target incarnation stamps are persisted in version 21.
The appended channel thinker callback roster and continuation tests are described in
[ability verification](ability-verification-review.md#dispatch-and-persistence).
