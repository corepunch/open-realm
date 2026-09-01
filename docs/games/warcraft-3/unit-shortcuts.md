# Persistent Hero And Idle-Worker Shortcuts

## Contract

Warcraft III's Hero buttons and idle-worker button are persistent gameplay HUD controls, not command-card abilities and not neutral-shop `Anei` interaction.

OpenRealm implements them as a server-authored `LAYER_UNIT_SHORTCUTS` layout. The game owns the roster, validates control, performs authoritative selection, and moves the authoritative camera. The client only maps F1-F8 to game commands and mirrors shortcut-driven authoritative selection into its local selection cache.

The current behavior is:

| Control | Action |
|---|---|
| Hero HUD button | Select the Hero when selectable and center the camera on it. |
| F1-F7 | First activation selects the corresponding controlled Hero; activating the same sole-selected Hero again centers the camera. |
| Idle-worker HUD button | Select and center the next idle worker, then advance the cycle cursor. |
| F8 | Same cycle operation as the idle-worker HUD button. |

The classic backtick/tilde idle-worker shortcut is not bound because OpenRealm currently reserves backtick for its developer console.

## Hero Roster

`G_UnitShowsHeroShortcut()` in `games/warcraft-3/game/g_shortcuts.c` accepts a unit when it:

- is an in-use `SVF_MONSTER`;
- is controlled by the viewing player through `G_UnitCanControl()`;
- has Hero attributes according to `G_UnitIsHero()`;
- is not a hidden training-queue entity;
- does not have `UnitUI.hideHeroBar` set.

Dead Heroes remain in the roster because Warcraft Hero identity survives death and OpenRealm keeps the Hero edict for revival. A dead Hero cannot become normal selection, but its shortcut can still center the camera on its current location.

Hero slots are stable with respect to the current entity ordering. F1-F7 address the first seven visible shortcut Heroes; the HUD itself is not artificially capped.

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

Each game client owns only:

```c
struct {
    BOOL dirty;
    DWORD last_idle_worker;
} shortcuts;
```

Relevant gameplay transitions mark `dirty`:

- relevant Hero/worker spawn/free;
- owner transfer;
- alliance/control changes;
- `Ahar` ability add/remove for harvest-only/custom workers;
- worker movement-state transitions where idle status changes;
- worker death;
- Hold Position;
- training completion;
- worker visibility/cargo transitions that change idle eligibility.

The generic free path calls the invalidation hook, but the hook rejects non-monsters and ordinary non-Hero/non-worker units before touching client dirty state. Projectiles, spell effects, destructables, and routine combat-unit destruction therefore do not cause shortcut-roster rescans.

`G_UpdateClientUnitShortcuts()` itself is only an O(number-of-clients) dirty check. It scans entities and serializes `LAYER_UNIT_SHORTCUTS` only for a dirty connected client. This avoids adding another per-frame entity scan on handheld targets such as the RG40XX-H.

Shortcut activations may perform bounded entity scans because those happen only in response to user input:

- F1-F7 finds the requested Hero slot;
- idle-worker cycling finds the next qualifying worker after the saved entity-number cursor.

A dirty HUD rebuild uses one combined entity pass to emit Hero buttons, count idle workers, and choose the next worker icon. Repeated rapid clicks remain correct even if the previous dirty layout has not reached the client yet: the server rejects the previously selected worker as a stale button hint and advances from `last_idle_worker`.

## Selection Synchronization

Normal world clicks update both server selection and the client's `cl.selection` cache directly. A server-authored layout button does not have that local side effect, so shortcut selection uses the existing `GameCommand` transport with command `wc3_selection`.

Flow:

```text
HUD/F-key command
    -> server validates target/control
    -> server replaces authoritative selection
    -> server refreshes portrait/commands
    -> GameCommand("wc3_selection", entity number)
    -> client replaces cl.selection
```

This prevents a shortcut from visually selecting one unit on the server while subsequent Smart orders or control-group assignment still use an older client-side selection.

No `entityState_t` or `playerState_t` network fields are added.

## HUD Placement

`games/warcraft-3/game/hud/hud_shortcuts.c` owns the layout constants.

- Hero buttons start at the top-left edge of the rendered world immediately below the upper menu and stack vertically.
- The idle-worker button is immediately above the minimap.
- The idle-worker count is a bottom-right text overlay on the worker icon.

Both use `FT_COMMANDBUTTON`, so they share the existing server-authored click/tooltip path rather than adding client-specific gameplay widgets.

## Lifecycle And Visibility

`LAYER_UNIT_SHORTCUTS` is a normal gameplay layer. Existing interface/cinematic `uiflags` behavior therefore hides it with the rest of the normal interface; no special cinematic client code is required.

Shared-control changes invalidate all shortcut layers because `G_UnitCanControl()` can change for units belonging to another player. Owner changes invalidate both the old and new control relationships.

## Known Gaps

The first implementation intentionally does not guess at retail behavior that is not already represented by reliable OpenRealm state:

- no health/mana overlays or low-health flashing on Hero shortcut buttons;
- no Hero skill-point indicator on the shortcut button;
- no dedicated dead/reviving visual treatment beyond retaining the Hero icon;
- no `SetReservedLocalHeroButtons` JASS/native implementation;
- no backtick idle-worker binding while that key owns the developer console;
- no retail-specific Hero-slot reordering beyond entity order.

These are presentation/compatibility additions and should not be implemented by reintroducing per-frame roster scans.

## Verification

Added unit tests cover the idle-worker predicate and shortcut dirty invalidation. The implementation also needs runtime verification after building:

1. Start a map with one Hero and several Peasants.
2. Confirm the Hero icon is below the upper menu and the worker icon/count is above the minimap.
3. Click the Hero icon from elsewhere on the map; selection and camera should move to that Hero.
4. Press F1 once from another selection, then F1 again; the first press selects and the second centers.
5. Leave three Peasants idle; confirm count `3`.
6. Repeatedly click the worker button or press F8; each activation should select/center a different idle Peasant and wrap.
7. Order one Peasant to harvest and confirm the count drops without periodic polling.
8. Stop that Peasant and confirm the count rises after its transition to plain stand.
9. Kill/free/transfer a worker and confirm the count/roster changes.
10. Kill and revive a Hero and confirm its persistent shortcut remains available.

No local compile or test execution is required to update this document; use the repository test commands in `CONTRIBUTING.md` when validating a built tree.

## See Also

- [Unit Selection And Control](selection-and-control.md)
- [Economy And Unit Presentation](economy-and-unit-presentation.md)
- [Hero Revival](hero-revival.md)
- [UI System](../../architecture/ui-system.md)
