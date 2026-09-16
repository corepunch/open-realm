# Persistent Hero And Idle-Worker Shortcuts

## Contract

Warcraft III's Hero buttons and idle-worker button are persistent gameplay HUD controls, not command-card abilities and not neutral-shop `Anei` interaction.

OpenRealm implements them as a server-authored `LAYER_UNIT_SHORTCUTS` layout. The game owns the roster, validates control, performs authoritative selection, and moves the authoritative camera. The client loads configurable key bindings that forward opaque game-command strings and mirrors shortcut-driven authoritative selection into its local selection cache.

The current behavior is:

| Control | Action |
|---|---|
| Hero HUD button | A single click selects the Hero. A second click on the same Hero within the 500 ms double-click window centers the camera. |
| Default F1-F7 | A single press selects the corresponding controlled Hero. A second press on the same Hero within the same 500 ms double-activation window centers the camera. |
| Idle-worker HUD button | Select and center the next idle worker, then advance the cycle cursor. |
| Default F8 | Same cycle operation as the idle-worker HUD button. |

The default bindings are declared in `games/warcraft-3/share/config.cfg`; user config overrides can bind any supported key to the same server command strings. The classic backtick/tilde idle-worker shortcut is not bound because OpenRealm currently reserves backtick for its developer console.

The same config path now owns the upper system-button defaults too: F9 opens
Quests, F10 routes to Menu, F11 routes to Allies, and F12 opens the single-player
Message Log. Those keys are not special-cased in `cl_input_w3.c`; they are normal
configurable bindings, consistent with F1-F8. Menu/Allies screen implementation
is separate from this shortcut subsystem.

## Hero Roster

`G_UnitShowsHeroShortcut()` in `games/warcraft-3/game/g_shortcuts.c` accepts a unit when it:

- is an in-use `SVF_MONSTER`;
- is controlled by the viewing player through `G_UnitCanControl()`;
- has Hero attributes according to `G_UnitIsHero()`;
- is not a hidden training-queue entity;
- is not currently hidden through `ShowUnit(false)` / `RF_HIDDEN`;
- does not have `UnitUI.hideHeroBar` set.

Dead Heroes remain in the roster because Warcraft Hero identity survives death and OpenRealm keeps the Hero edict for revival. A dead Hero cannot become normal selection, but its shortcut can still center the camera on its current location.

Hero slots use the same stable Warcraft selection ordering as the multiselect status panel: unit-type priority descending, unit level descending, then canonical Warcraft rawcode descending, with entity scan order preserved for equal entries. This keeps a group of selected Heroes in the same top-to-bottom order in both presentations. F1-F7 address the first seven Heroes in that ordered roster; the HUD itself is not artificially capped.

The button art uses `UnitProfile.art`, the same unit icon source used by train/revive presentation. This keeps the shortcut race/unit-specific without introducing a hard-coded asset path.

## Idle Worker Definition

`G_UnitIsIdleWorker()` deliberately uses gameplay state instead of animation alone. A unit is an idle worker only when it:

- is alive, visible, in use, and a monster unit;
- is not a building or training-queue entity;
- has worker capability from authored unit data: a non-empty construction list, or `Ahar` for harvest-only/custom workers;
- is not inside a Gold Mine;
- is not holding position;
- is in a plain `stand` move with no active ability.

This excludes workers that are harvesting, waiting for a mine slot, returning resources, repairing, constructing, moving, attacking, or holding position even if one of those states happens to render a stand-like animation.

`G_UnitShowsIdleWorkerShortcut()` then adds the same per-player `G_UnitCanControl()` authority check used elsewhere in Warcraft selection/order code.

## Event-Driven Invalidations

The subsystem does **not** scan all entities each rendered/server frame.

Each connected player on the game server owns only:

```c
struct {
    BOOL dirty;
    DWORD last_idle_worker;
} shortcuts;
```

Relevant gameplay transitions mark `dirty`:

- relevant Hero/worker spawn/free;
- owned Hero skill-point changes, so the shortcut badge updates immediately;
- real damage to an owned Hero that has a shortcut, once per damage event, so the refreshed alert deadline reaches the client;
- owner transfer;
- alliance/control changes;
- `Ahar` ability add/remove for harvest-only/custom workers;
- worker movement-state transitions where idle status changes;
- worker death;
- Hold Position;
- training completion;
- worker visibility/cargo transitions that change idle eligibility;
- `ShowUnit` visibility transitions for Heroes, so a scripted hidden Hero leaves the roster immediately and returns when shown.

The generic free path calls the invalidation hook, but the hook rejects non-monsters and ordinary non-Hero/non-worker units before touching the player's server-side dirty state. Projectiles, spell effects, destructables, and routine combat-unit destruction therefore do not cause shortcut-roster rescans.

`G_UpdateClientUnitShortcuts()` itself is only an O(number-of-clients) dirty check. It scans entities and serializes `LAYER_UNIT_SHORTCUTS` only for a dirty connected client. This avoids adding another per-frame entity scan on handheld targets such as the RG40XX-H.

Shortcut activations may perform bounded entity scans because those happen only in response to user input. Hero HUD clicks and F1-F7 then enter the same-Hero 500 ms activation tracker, so input source does not change single-vs-double activation semantics:

- F1-F7 finds the requested Hero slot using the same ordering as the visible Hero-button roster and multiselect panel;
- idle-worker cycling finds the next qualifying worker after the saved entity-number cursor.

A dirty HUD rebuild uses one combined entity pass to collect and stably order Hero buttons while also counting idle workers and choosing the next worker icon. The temporary Hero pointer list exists only during that dirty rebuild; there is still no per-frame roster scan. Repeated rapid clicks remain correct even if the previous dirty layout has not reached the client yet: the server rejects the previously selected worker as a stale button hint and advances from `last_idle_worker`.

## Selection Synchronization

Normal world clicks update both server selection and the client's `cl.selection` cache directly. A server-authored layout button does not have that local side effect, so shortcut selection uses the dedicated `svc_set_selection` protocol event.

Flow:

```text
HUD/F-key command
    -> server validates target/control
    -> server replaces authoritative selection
    -> server refreshes portrait/commands
    -> svc_set_selection(count, entity numbers)
    -> client replaces cl.selection
```

This prevents a shortcut from visually selecting one unit on the server while subsequent Smart orders or control-group assignment still use an older client-side selection.

No `entityState_t` or `playerState_t` network fields are added.

## HUD Placement

`games/warcraft-3/game/hud/hud_shortcuts.c` owns the layout constants.

- Hero buttons start at the top-left edge of the rendered world immediately below the upper menu and stack vertically.
- The idle-worker button keeps its authored vertical position immediately above the minimap on 4:3, but on widescreen its horizontal offset is measured from the physical left edge rather than from the centered 4:3 HUD safe area.
- The idle-worker count is a bottom-right text overlay on the worker icon.
- A Hero with unspent skill points uses the same bottom-right number overlay, showing the current `hero.skillpoints` value and omitting the number at zero.
- Real damage to an owned Hero refreshes a transient red pulse on that Hero shortcut. The server rebuilds the shortcut once with an absolute expiry timestamp; the client animates the tint locally until expiry, so there is no per-frame shortcut serialization.

The shortcut layer emits an invisible `FT_SIMPLEFRAME` root with `UIFLAG_EXTEND_WIDESCREEN_X`. Hero buttons, the idle-worker button, and its count are parented to that root using the same authored offsets and sizes as before. On 4:3 the root is still `0.8 x 0.6`, so placement is unchanged; on a wider display only the root expands to the full UI canvas, putting the shortcut controls against the physical left edge while the normal ConsoleUI remains centered in its 4:3 safe area.

Both buttons use `FT_COMMANDBUTTON`, so they share the existing server-authored click/tooltip path rather than adding client-specific gameplay widgets.

## Lifecycle And Visibility

`LAYER_UNIT_SHORTCUTS` is a normal gameplay layer. Existing interface/cinematic `uiflags` behavior therefore hides it with the rest of the normal interface; no special cinematic client code is required.

Shared-control changes invalidate all shortcut layers because `G_UnitCanControl()` can change for units belonging to another player. Owner changes invalidate both the old and new control relationships.

## Hero Damage Alert

`T_Damage()` calls the shortcut alert hook only after invulnerability and Mana Shield processing have left positive damage. Destructables and ordinary non-Hero units are ignored. The alert belongs to the Hero's owning player rather than every ally with shared control; shared-control Hero shortcuts keep their normal selection behavior without receiving the owner's combat warning.

The server stores only a transient deadline on the Hero edict and marks the owner's shortcut layer dirty. `hud_shortcuts.c` serializes `UIFLAG_ALERT_RED_PULSE` plus that absolute millisecond deadline into the Hero command button. The generic client command-button renderer uses a 500 ms triangle-wave pulse and returns automatically to the normal untinted icon when the deadline passes. The transient deadline is presentation state and is intentionally not persisted by save/load.

## Known Gaps

The first implementation intentionally does not guess at retail behavior that is not already represented by reliable OpenRealm state:

- no health/mana overlays or low-health flashing on Hero shortcut buttons;
- no dedicated dead/reviving visual treatment beyond retaining the Hero icon;
- no `SetReservedLocalHeroButtons` JASS/native implementation;
- no backtick idle-worker binding while that key owns the developer console;

These are presentation/compatibility additions and should not be implemented by reintroducing per-frame roster scans.

## Verification

Unit tests cover the idle-worker predicate, shortcut dirty invalidation, and that Hero HUD/F-key ordering matches the multiselect status-panel ordering. The implementation also needs runtime verification after building:

1. Start a map with one Hero and several Peasants.
2. Confirm the Hero icon is below the upper menu and the worker icon/count is above the minimap.
3. Click the Hero icon once from another selection; only selection should move to that Hero. Double-click the Hero icon; the second click should center the camera. A later isolated click must not center it.
4. With multiple Heroes, select them together and confirm the persistent Hero icons appear in the same order as those Heroes in the status-panel multiselect grid; F1-F7 must address that same order.
5. Press F1 once from another selection; only selection should change. Press F1 again within 500 ms to center. After waiting longer than 500 ms, another isolated F1 press must not move the camera even though the Hero is already selected.
6. Leave three Peasants idle; confirm count `3`.
7. Repeatedly click the worker button or press F8; each activation should select/center a different idle Peasant and wrap.
8. Order one Peasant to harvest and confirm the count drops without periodic polling.
9. Stop that Peasant and confirm the count rises after its transition to plain stand.
10. Kill/free/transfer a worker and confirm the count/roster changes.
11. Give a Hero one or more unspent skill points and confirm the number appears in the lower-right of its shortcut; spend the final point and confirm the number disappears.
12. Damage the owned Hero and confirm its shortcut pulses red for several flashes without affecting the portrait art or selection behavior. Repeated damage should refresh the warning window.
13. Hide a Hero with `ShowUnit(hero, false)` and confirm its shortcut disappears; show it again and confirm the shortcut returns.
13. Kill and revive a Hero and confirm its persistent shortcut remains available.

No local compile or test execution is required to update this document; use the repository test commands in `CONTRIBUTING.md` when validating a built tree.

## See Also

- [Unit Selection And Control](selection-and-control.md)
- [Economy And Unit Presentation](economy-and-unit-presentation.md)
- [Hero Revival](hero-revival.md)
- [UI System](../../architecture/ui-system.md)
- [Quest And Message Log UI](quest-and-message-log-ui.md)
