# Persistent Hero And Idle-Worker Shortcuts

## Contract

Warcraft III's Hero buttons and idle-worker button are persistent gameplay HUD controls, not command-card abilities and not neutral-shop `Anei` interaction.

OpenRealm implements them as a server-authored `LAYER_UNIT_SHORTCUTS` layout. The game owns the roster, validates control, performs authoritative selection, and moves the authoritative camera. The client loads configurable key bindings that forward opaque game-command strings and mirrors shortcut-driven authoritative selection into its local selection cache.

The current behavior is:

| Control | Action |
|---|---|
| Hero HUD button | A single click selects the Hero. A second click on the same Hero within the 500 ms double-click window centers the camera. While an entity-target command is active, clicking the Hero sends it to that command's target validation callback instead of cancelling targeting or changing selection. |
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

Each connected player owns an invalidation flag, an idle-worker cycle cursor, and a runtime-only cached Hero roster. Each cached entry records the Hero's entity ID, spawn identity and last transmitted compressed vitals. The cache is cleared on disconnect and discarded on load.

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

`G_UpdateClientUnitShortcuts()` checks connected clients each server tick. It scans all entities and serializes `LAYER_UNIT_SHORTCUTS` only for a dirty client; otherwise it validates and examines only that client's cached Hero roster and unicasts compressed vitals only when values change. This avoids an additional per-frame **world-entity scan** on handheld targets such as the RG40XX-H.

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

`games/warcraft-3/game/hud/hud_shortcuts.c` owns the shortcut frame geometry.

- Hero buttons start at the top-left edge of the rendered world immediately below the upper menu and stack vertically.
- The idle-worker button keeps its authored vertical position immediately above the minimap on 4:3, but on widescreen its horizontal offset is measured from the physical left edge rather than from the centered 4:3 HUD safe area.
- The idle-worker count is a bottom-right text overlay on the worker icon.
- A Hero with unspent skill points uses the same bottom-right number overlay, showing the current `hero.skillpoints` value and omitting the number at zero.
- Each Hero icon has a health bar coloured by its life fraction (red at 0%, yellow at 50%, green at 100%), followed by a cyan mana bar when the Hero has a mana pool. The vertical slot spacing accommodates both without overlapping the next portrait. Dead Heroes keep their icon and display an empty health fill.
- During an active unit-target command, a Hero icon can be clicked as a target even when that Hero is not in the current selection. The server validates it through the active game target callback; rejected targets leave targeting active. A target click does not count toward Hero select/double-click camera activation.
- Real damage to an owned Hero refreshes a transient red pulse on that Hero shortcut. The server rebuilds the shortcut once with an absolute expiry timestamp; the client animates the tint locally until expiry, so there is no per-frame shortcut serialization.

The shortcut layer emits an invisible `FT_SIMPLEFRAME` root with `UIFLAG_EXTEND_WIDESCREEN_X`. Hero buttons and their passive bars, the idle-worker button, and its count are parented to that root. On 4:3 the root is still `0.8 x 0.6`; the Hero slot stride is now `0.046` (formerly `0.037`) to make space for the two bars, while the first Hero's top-left anchor and the idle-worker button are unchanged. On a wider display only the root expands to the full UI canvas, putting the shortcuts against the physical left edge while ConsoleUI remains centered in its 4:3 safe area.

Both buttons use `FT_COMMANDBUTTON`, so they share the existing server-authored click/tooltip path rather than adding client-specific gameplay widgets.

### Shared Health Bar Colouring

The Hero shortcut fills, world-hover health bars and multi-unit status-panel health bars all use the same smooth health fraction tint: 0% red, 25% orange, 50% yellow, 75% yellow-green, 100% green. The client derives the tint from the same current fraction used to clip the fill. WC3 authoring marks hover and Hero bars with `UIFLAG_HEALTH_GRADIENT`; multiselect HP fills use the shared helper directly. Mana, cargo and unrelated status bars retain their existing colours. The black backings remain independent of this fill tint.

### Live Hero Health And Mana Bars

The HUD vital opcode requires network protocol 24. Protocol 23 peers must be
rejected during connection setup: their message dispatcher cannot skip the new
opcode and would abandon the remainder of a datagram. Native save version 143
and browser save version 3 are separate contracts; prior saves are rejected,
and the runtime vital cache is discarded and rebuilt after loading.

`wc3_shortcuts.vital_authorization_matrix_preserves_world_fog` checks enemy,
passive-only, control-without-passive, basic shared control, advanced shared
control, and revoked-control recipients. It checks both cached-roster sends and
`globals.CanSeeEntity`: granting HUD vitals must leave hidden world entities
excluded. Authorization follows the existing directional
`level.alliances[viewer][owner]` selection/control contract.

`hud_shortcuts.c` emits *both* passive `FT_SIMPLESTATUSBAR` frames for each Hero. Art uses the multiselect status panel's `SimpleHpBarConsole` and `SimpleManaBarConsole`. Health fill uses the shared continuous red-to-yellow-to-green life gradient, whereas mana remains cyan. The client hides mana bars for Heroes without a mana pool, including when max mana changes to zero, without resending the layout. Portrait commands retain selection, tooltips, skill-point badges, and damage alerts. The native-frame geometry uses inline normalized UI coordinates and stays entirely in the WC3 game module.

The generic renderer in `client/cl_scrn.c` interprets `UIFLAG_ENTITY_STATUSBAR` as a binding: `uiFrame_t.value` is the integral entity number and `uiFrame_t.stat` is `ENT_HEALTH` or `ENT_MANA`. `svc_ui_entity_stats` supplies only `(entity number, compressed HP, compressed mana, has_mana)` to the recipient's HUD cache, not the unit's world position. The client clips the statusbar image/UV by the live normalized fraction. Invalid bindings or missing cache entries are not drawn. This does not change the ordinary statusbar/playerstate/context bindings.

**Fog and control:** a shared-control Hero can be present in the recipient's shortcut roster despite missing from their world snapshots (shared control does not grant shared vision). The server therefore sends small recipient-filtered HUD vital updates independently of world-entity snapshot admission; it never forces that Hero into `sv_ents.c` or reveals fog. The game caches the roster during each existing dirty-layer rebuild, validates entity incarnation and control rights, and checks only those Hero entries at each subsequent simulation tick. Changed compressed vitals/pool-presence bytes are sent first; because these recipient unicasts ride the unreliable channel message, a bounded roster sweep resends unchanged values every 10 simulation frames so a lost update converges without another health change. The sweep sends at most 32 entries (163 payload bytes) per band and wraps through larger rosters. Each rebuilt roster emits a clear-and-full update, and connection termination and load/reconnect clear the runtime cache and sweep cursor. The save version is incremented because `gameClient_t`'s in-memory layout changed, even though the runtime cache and sweep fields are excluded from serialization.

## Lifecycle And Visibility

`LAYER_UNIT_SHORTCUTS` is a normal gameplay layer. Existing interface/cinematic `uiflags` behavior therefore hides it with the rest of the normal interface; no special cinematic client code is required.

Shared-control changes invalidate all shortcut layers because `G_UnitCanControl()` can change for units belonging to another player. Owner changes invalidate both the old and new control relationships.

## Hero Damage Alert

`T_Damage()` calls the shortcut alert hook only after invulnerability and Mana Shield processing have left positive damage. Destructables and ordinary non-Hero units are ignored. The alert belongs to the Hero's owning player rather than every ally with shared control; shared-control Hero shortcuts keep their normal selection behavior without receiving the owner's combat warning.

The server stores only a transient deadline on the Hero edict and marks the owner's shortcut layer dirty. `hud_shortcuts.c` serializes `UIFLAG_ALERT_RED_PULSE` plus that absolute millisecond deadline into the Hero command button. The generic client command-button renderer uses a 500 ms triangle-wave pulse and returns automatically to the normal untinted icon when the deadline passes. The transient deadline is presentation state and is intentionally not persisted by save/load.

## Known Gaps

The first implementation intentionally does not guess at retail behavior that is not already represented by reliable OpenRealm state:

- no additional low-health flashing tied to the Hero bars (the existing real-damage red pulse remains on the portrait);
- no dedicated dead/reviving visual treatment beyond retaining the Hero icon;
- no `SetReservedLocalHeroButtons` JASS/native implementation;
- no backtick idle-worker binding while that key owns the developer console;

These are presentation/compatibility additions and should not be implemented by reintroducing per-frame roster scans.

## Verification

Unit tests cover the idle-worker predicate, shortcut dirty invalidation, Hero bar ordering/relative positions, the visibility of mana bars with dynamically acquired pools, recipient-authorized value updates, and the client packet-to-render statusbar binding. The implementation also needs runtime visual verification after building:

1. Start a map with one Hero and several Peasants.
2. Confirm the Hero icon is below the upper menu and the worker icon/count is above the minimap.
3. Click the Hero icon once from another selection; only selection should move to that Hero. Double-click the Hero icon; the second click should center the camera. A later isolated click must not center it. Arm a unit-target healing or positive-buff ability and click a top-left Hero icon, including one outside the current selection; confirm the ability targets that Hero and rejected targets leave targeting active.
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
14. Kill and revive a Hero and confirm its persistent shortcut remains available.
15. Damage/heal a Hero and spend/regenerate mana while it is not selected; confirm the green/cyan bars update directly underneath its top-left icon without requiring a selection or shortcut rebuild.
16. Confirm multiple Hero bars do not overlap adjacent icons at both 4:3 and widescreen resolutions; verify a Hero without mana shows only the green bar, and a dead Hero shows an empty health bar until revived.

The dedicated headless regressions include `wc3_shortcuts.hero_button_targets_visible_hero_during_entity_targeting`, `wc3_shortcuts.hero_buttons_match_multiselect_order`, `wc3_shortcuts.hud_buttons_share_full_canvas_left_root`, `wc3_shortcuts.controlled_hero_vitals_are_delta_sent_without_vision_and_follow_mana_pool`, `client_layout.hero_statusbars_decode_out_of_view_vitals_and_mana_pool_changes`, and `client_layout.hero_statusbars_clear_roster_on_reuse_and_ignore_malformed_packets`. Static checks cannot verify framebuffer pixels, real network behaviour or ROC/TFT art availability; verify the bars in both game variants at 4:3 and widescreen after building.

## See Also

- [Unit Selection And Control](selection-and-control.md)
- [Economy And Unit Presentation](economy-and-unit-presentation.md)
- [Hero Revival](hero-revival.md)
- [UI System](../../architecture/ui-system.md)
- [Quest And Message Log UI](quest-and-message-log-ui.md)
