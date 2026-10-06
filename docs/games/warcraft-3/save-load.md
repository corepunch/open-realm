# Warcraft III Save/Load

## Contract

The WC3 game module owns save/load. `GetGameAPI()` exposes `SaveGame` and `LoadGame` callbacks through `server/game.h`; the JASS `SaveGame` and `LoadGame` natives use the same callbacks and resolve names through `gi.SavePath`.

`WriteGame()` writes the current game state to a versioned binary file. The file contains:

- `W3SV` magic, format version 112, canonical map path, the current `sizeof(edict_t)`, entity count, client count, script identity, and native-handle registry counts;
- mutable fine terrain plus independently published four-lane adaptive hierarchy dimensions/classes;
- level frame/time, authoritative Warcraft time-of-day state, map-global camera bounds, and started/script-started flags;
- each client `GAMECLIENT` state, including its `PLAYER` state, JASS settings and dynamically sized `SetPlayerAbilityAvailable` rawcode list, runtime removed/result-presentation state, researched tech, text storage, camera values, messages, and HUD caches;
- each camera target as an entity index;
- the quest and quest-item graph's strings and status flags;
- the initial point-order waypoint reserve and its allocation cursor, plus additional managed destination edicts;
- one used flag per entity slot, a raw `edict_t` block for used slots, logical attack defaults/overrides, immutable sound profiles, bounded animation text, and retained native fine-route points;
- sparse lifecycle records for all 29 edict pools, written after the edicts, with per-pool counts and owning-edict indexes;
- ordinary fine-cell active insertion history after the pools: a64-bit publication counter, object count, owning-edict indexes and retained per-cell ranks;
- basic attack projectiles retain their launch-time attack type, and fixed-point artillery projectiles retain their launch-time attack type and splash profile, in the serialized edict; attack cooldowns persist as simulation-time deadlines and keep elapsing across order changes;
- group membership, trigger enabled state, timer state, weather-effect registry state, unread gameplay events, and a semantic JASS VM snapshot;
- a `W3OK` commit footer and FNV-1a checksum over the complete preceding payload.

`WriteGame()` removes the destination when any record or footer write fails. `ReadGame()` validates the commit footer, checksum, format, script identity, and quest/group/trigger/timer/event registry counts before mutating clients or entities. A truncated or rejected partial write therefore cannot become a loadable artifact or clear the live world. A header mismatch names the failing field and prints saved versus live counts; do not treat a generic `header mismatch` line as complete.
Quest objects and items are restored in place so the running JASS VM's light handles keep their object identity. Events use `MAX_EVENTS` fixed slots, quests use `MAX_QUESTS` slots, and each quest owns `MAX_QUESTITEMS` item slots; `inuse` marks lifecycle state without moving live pointers during removal. Event references use physical slot IDs, so retired region-event slots may leave holes; the loader checks that a referenced slot is in use instead of assuming active slots form a dense prefix. Map startup can recreate a region registration that the saved state had removed, and the saved event table restores that removal. Loading rejects a quest or item count mismatch instead of leaving those handles dangling. Loading completely reloads the saved map first, then applies state.

The versioned layout retains the authoritative `level.timeofday` record and game-state event condition fields (`state`, `limitop`, `limitval`) and the client removal/pending-result fields used by victory/defeat presentation. Quest and event records are written by the recursive field schema. Counted descriptors write the count followed by the array prefix. Since version 13 the dynamic JASS group registry is written immediately after the level-field stream: every handle ordinal through `level.num_groups` writes `ggroup_t.inuse`, `num_units`, and that many `F_EDICT` indexes. Inactive holes remain serialized so higher live handle ordinals do not shift. Version 14 adds `GAMEEVENT.value`, the scalar callback payload used by research events, and pairs it with JASS snapshot format 3 so a sleeping callback preserves `JASSCONTEXT.eventValue` across save/load. Version 17 adds `GAMEEVENT.point` / `has_point` and pairs it with JASS snapshot format 4 so point-target spell response context survives unread event queues and yielded trigger coroutines. Version 77 appends a disabled-ability count and that many rawcodes after each client record and camera-target index. The list stores `SetPlayerAbilityAvailable(..., false)` state; capacity and pointer remain runtime allocations. The version-59 layout cannot be read by version 77 because its client-record boundary has no list count; version 77 saves are likewise rejected by older exact-version readers.

Version109 retains the three coarse scheduler budgets per player and both
route-owned admission records. Queue insertion ranks, request times, policy,
owner, pending state, work and countdown are authoritative. Intrusive links and
bucket heads/tails are runtime fields, rebuilt from unique saved ranks only
after physical groups and unit routes have loaded. Invalid rank, class, policy,
count, countdown or work rejects restoration; failed restoration clears all
coarse queues before releasing groups. Version108 and older layouts are rejected.
See [coarse/fine contention](retail-pathfinding-engine.md#coarse-and-fine-contention-retain-independent-player-fifos).

Version102 moves the eight ordered status records out of the raw edict into an
optional pool. Units with no applied statuses own no record storage. Insertion
allocates all eight slots together, and removal retains the allocation until
edict release so synchronous inverse callbacks keep stable slot addresses.
The raw pointer is excluded from the edict payload; the pool serializes every
slot, including vacant ones, and remaps each applying source through `F_EDICT`.
Timing, rank, origin rawcode and numeric payloads remain unchanged. Loading
restores absent storage as null. Version101 and earlier layouts are rejected.

Version106 separates shared immutable unit sound profiles from mutable pending
events. Save records contain logical variant arrays/counts and attack/death sound
indices, never profile pointers. Load re-interns values and rejects oversized
variant counts. Selection response requests still clear during load; owner/world
pending events retain their existing saved-state behavior.

Version105 adds the route-owned world-request memo fields. They are derived
`FIELD_RUNTIME` data, cleared in both unit and group route serializers; restored
native goals and route points remain authoritative. Older layouts are rejected.

Version104 replaces the inline attack copies with shared immutable profiles and
optional per-slot overrides. Each edict payload stores a two-bit ownership mask,
both logical default profiles and the values of any owned overrides. Restore
interns immutable defaults and allocates only saved overrides. No process pointer
is serialized. Pool reset runs before restoring edicts so it cannot discard
newly restored attack overrides. The other 29 lifecycle pools retain their own
later payloads. Version103 and all earlier layouts are rejected.

Version103 moves the sixteen-entry command ring into an optional pool. A fresh
unit has no queue allocation. The first queued order allocates the full ring;
clearing or consuming it retains storage until edict release, preserving entry
addresses during synchronous cancellation callbacks. Head and count remain in
the raw edict; the pool stores every entry, including unused slots, through the
existing queued-order schema. Loading rejects out-of-range counters or a nonempty
ring without storage. Wrapped FIFO order and Move context survive reconstruction.
Version102 and earlier saves are rejected by the exact-version guard.

Version85 retains unit owned-pool insertion sequences and the64-bit allocation
counter. Birth and genuine owner transfer insert a unit at the pool head; a
same-owner call preserves its position. Captain recruitment therefore remains
ordered across removal, reused entity slots and saves taken before AI startup.
Duplicate, out-of-range and missing spawned-unit sequences reject save/load.
Four complete retail controls match 908 physical commits and 4344 saved suffix
commits, including explicit bot-VM retirement before loading. See
[captain owned-pool order](retail-pathfinding-engine.md#captain-owned-pool-order-survives-transfer-and-reused-slots).

Version79 additionally retains each unit’s latest submitted shared Move request,
physical owner creation sequences and the next sequence. FIFO activation does
not overwrite submission history; reused owner slots do not acquire an extra
visit after loading. Zero, duplicate and out-of-range sequences are rejected.
The two-pending-Shift regression repeats864 saved motion suffix commits, including
a future external click after the earlier save. Version78 and all earlier layouts
are rejected. See [queued group history](retail-pathfinding-engine.md#two-pending-shift-moves-retain-submission-history-and-physical-generations).

The mixed active/idle Shift regression also reproduces1020 saved suffix commits
under version79. No layout change is needed: common queued contexts, immediate
idle activation and later cohort joining use the existing retained records. See
[mixed Shift](retail-pathfinding-engine.md#mixed-active-and-idle-shift-share-one-submitted-request).

Two independent active singleton point owners additionally reproduce844 saved
suffix commits under version79, before and after later shared acquisition. See
[independent owners](retail-pathfinding-engine.md#independent-active-owners-accept-the-same-pending-ground-request).

Version78 retained Move-owned queued request context and active cohort request
identity. Common point orders survive before activation; staggered activation
creates fresh physical cohorts with deterministic member order. Saves on either
side of the transition reproduce the original motion suffix. Version77 added
the merged upstream Stop guard and disabled-ability list; both remain retained.

Version76 partitions ordinary fine admission into sixteen player rows and saves
`movement.fine_class` so cancellation can unlink the old row before changing
owner. Work/countdown and FIFO head/tail/count are recursive records with mapped
edict pointers. Validation rejects cross-row ownership, duplicate membership,
invalid classes and malformed links before following them. Pending requests in
all sixteen rows survive a real save/load; ordinary public ownership cancellation
matches original final motion words before and after saved continuations. See
[ordinary player queues](retail-pathfinding-engine.md#ordinary-fine-search-belongs-to-the-unit-player).

Version62 adds `movement.fine_pose` and `movement.pose_valid`. Move keeps native
fine-grid words between commits because published world coordinates can lose
low bits. The raw primitive edict record retains both fields, and an eight-step
resume matches the original fixture's fine/world/velocity/facing words.
Rejected step previews leave them unchanged; explicit Move world commits
invalidate the retained pose. The exact-version guard rejects61 and earlier
layouts. JSVM remains7. See [retained fine pose](retail-pathfinding-engine.md#retained-fine-pose-reaches-move).

Version61 adds `movement.flat_speed_bonus`, the flat bonus last published to
Move by an accepted order or speed setter. It is independent of the current
inventory, so loading after Boots removal preserves the original committed
cap and subsequent position/velocity words. The primitive edict record owns
this field; no callback or pointer relocation is added. The exact-version guard
rejects60 and earlier layouts. JSVM remains7. See
[flat bonus publication](retail-pathfinding-engine.md#flat-bonuses-retain-their-publication-state).

Version67 persists the complete Move-owned fine route: count, current index,
lane mask and native coordinate words. The process-owned pointer is excluded
from the raw persistent record and reallocated from the explicit point payload;
invalid counts, indices and nonfinite coordinates are rejected. Reload releases
old curves before replacing edicts. The complete controlled wall detour resumes
with the same remaining22 retail position/velocity/heading steps. Formats66 and
earlier are incompatible; JASS snapshot7 and wire messages are unchanged. See
[retained fine routes](retail-pathfinding-engine.md#retained-fine-routes-reproduce-a-complete-retail-detour).

### Save compatibility policy

Save compatibility is deliberately unsupported. Load only the current format version and serialized layout, and reject older or otherwise mismatched saves with a diagnostic. Bump the format version when the serialized layout, callback identity, or meaning changes, including changes that leave `sizeof(edict_t)` unchanged. Do not retain legacy layouts, migration defaults, compatibility aliases, or optional extension records.

ORDER-01.5 extends the semantic JASS snapshot to version7 with callback
`eventType`. Issued-order value/point/target snapshots already use the existing
game-event fields; event type gates now remain correct after a yielded callback
is restored. Both unread issued-order events and sleeping actions have round-trip
coverage. This callback change left outer W3SV56 and the network protocol unchanged, but the
embedded exact-version guard rejects a version6 JASS snapshot. See
[immutable issued-order callbacks](issued-target-order-events.md#immutable-callback-ownership).

The server's map-selection read checks both the format version and entity size before reloading a map. The state reader applies the same guards, validates the existing checksum and reference domains, and requires the current payload to end at the commit footer.

Version 40 added the region registry and region/event context. Its rejection of version 39 saves was intentional; later versions follow the same exact-match policy.

Version 77 adds Stop guard movement state to the entity record: `guard_position` and one `guard_state` enum (`NONE`, `IDLE`, `COMBAT`, or `RETURNING`). The exact-version guard rejects earlier saves because their entity records lack these fields.
Upstream version 65 makes delayed ability-edict identity snapshots mandatory for owner/target-bound thinkers. Pocket Factory, Graveyard/Exhume production, Stasis Trap, Divine Shield, and Lightning Shield persist `channel->owner_spawn_time` (and Lightning Shield `target_spawn_time`) so a recycled edict slot cannot inherit an older delayed effect. Version 64 saves are rejected rather than loading those thinkers without generation guards.

Upstream version 64 removes redundant Sacrifice/Polymorph `active` and destructable `initialized` fields. Their pool pointer represents ownership directly; Polymorph death releases its inverse record while keeping death presentation. Version 63 saves are rejected because those serialized pool layouts changed.

Format 90 merges the retained pathfinding/AI state with the full27-pool lifecycle layout.
It rejects both branch format89 and upstream format64: entity layouts and streams differ.
The upstream63/64 pool-presence and redundant-flag changes are retained in the combined format.

Version 41 persists region and region-event handle generations and exhaustion state. This keeps each recycled handle's `GetHandleId` unique during a session and stable across save/load. The exact-version guard rejects version 40 saves as well as earlier versions.

Version 42 persists the unit incarnation (`spawn_time`) captured by unit-bound trigger registrations. This prevents an event from attaching to a different unit when the original edict slot is reused after load. The exact-version guard rejects version 41 saves as well as earlier versions.

Version 32 adds `edict_t.permanent_health_bonus`, the persistent ledger for
research-owned maximum-life effects such as `UpgradeData.slk` `rhpx`. The field
is serialized explicitly and the version/`sizeof(edict_t)` guard rejects older
records rather than allowing shifted edict state. Hero stat recomputation uses
the restored ledger so a later Strength change cannot erase an upgrade-owned
maximum-health bonus.

Version 33 adds `edict_t.projectile_reflected` for basic attack missiles. A
Defend-returned projectile can therefore be saved while travelling back to its
source without becoming eligible for a second reflection after load. The field
is part of the normal edict schema and follows the existing `goalentity` /
`owner` entity-index relocation contract.

The version 3 layout expands the fixed-size `GAMECLIENT` cinematic camera state with target Z offset, near/far clipping planes, and target-controller orientation inheritance. Version 2 saves are rejected because the raw client record layout changed; this prevents older saves from being misread with shifted fields.

The version 4 layout packs `PLAYER.cinematic_portrait`, `team`, `color`, and `race` as consecutive `BYTE`s (one `NFT_LONG` on the wire), drops `PLAYER.camera_bounds` (the rectangle lives on `level`), and writes `level.camera_bounds` with the other level clocks. Version 3 saves are rejected because both the raw `GAMECLIENT` record and the level stream changed.

Version 10 extends the authoritative `level.timeofday` record with the Warsmash-style temporary/false clock (`hour`, `minute`,
remaining simulation ticks, active, initialized), so a loaded save cannot silently resume the canonical day/night cycle while a saved
Moonstone-style override should still be active. Version 11 adds per-slot JASS group lifecycle state so destroyed group slots can be safely recycled and restored. Version 12 expands the raw `GAMECLIENT` snapshot with semantic Warcraft music state (map/default selection, current source, start/seek position, pause state, and music/thematic volumes); version 36 extends that state with session identity and persisted thematic restoration. Version 13 replaces the fixed inline group array with a growable stable-pointer registry and serializes that registry separately, so v12 and earlier saves are rejected rather than being interpreted with the wrong level layout. Version 15 accompanies the natural-creep sleep edict fields. Version 16 adds `level.environment_fog.active` and `.defaults` so scripted distance mist and the `ResetTerrainFog` target survive save/load.
Version 17 accompanies the WC3 `edict_t` vertex-colour fields used by `SetUnitVertexColor`; the changed edict size and format version reject older records instead of interpreting shifted state.
Version 17 also adds dedicated per-unit `abilitycooldowns[]` records so active ability cooldown windows survive save/load without sharing the timed buff/status array. Spell point response context in queued events and the matching JASS snapshot state are part of the same version. Version 18 adds the per-unit Polymorph restoration record (source ability/buff, selected form, original model/scale/movement speed, and active flag), paired with the already-persistent timed buff so an active morph restores correctly after load.
Version 19 adds the ability-owned per-unit Raven Form takeoff state (`fly_height`, rise start/duration, and rise phase) so a save taken during the authored post-morph ascent cannot restore with an invalid altitude transition. `raven_fields` explicitly serializes those four scalar fields; no new edict pointers are introduced.
Version 20 adds race-specific construction lifecycle state and an explicit `F_EDICT` fixup for `construction.worker`; v19 saves are rejected rather than interpreting the expanded raw edict record.
Version 21 adds `mineoverlay.parent` and `acolyte_mine.mine` `F_EDICT` fixups plus overlay income timing/index and Acolyte slot/generation state. The original `Agld` edict remains the finite resource owner for Haunted/Entangled overlays, so those relationships must survive load rather than being reconstructed from proximity.
Version 22 adds neutral-shop stock state: global item/unit slot capacities plus each shop edict's initialized item-stock entries, current counts, and absolute start/replenish deadlines. `stock_fields` bounds `item_count` by `MAX_SHOP_STOCK`; the deadlines use the restored simulation clock, so partially elapsed restock timers resume without rebasing. The expanded `edict_t` layout and format version reject older records instead of interpreting shifted state.
Version 23 adds the fixed TimerDialog registry. Each live slot persists its timer association, title/colour presentation state, and client visibility mask; non-null JASS `timerdialog` values snapshot as stable slot indexes. Layout payloads remain transient, so load invalidates the timer-dialog HUD cache and republishes restored state on the next frame. See [Timer Dialogs And Mission Countdowns](timer-dialogs.md).
Version 24 adds the fixed leaderboard registry, ordered item/style/color state, per-player leaderboard assignment indexes, per-client display masks, and stable JASS `leaderboard` handle IDs. Leaderboard layout payloads remain transient and are republished after load. See [Leaderboards And Counted Objective HUDs](leaderboards.md).
Version 28 extends neutral-shop persistence with initialized `Sellunits` stock entries, current counts, and absolute start/replenish deadlines. `stock_fields` bounds both `item_count` and `unit_count` by `MAX_SHOP_STOCK`, so a partially replenished Mercenary Camp resumes against the restored simulation clock without reconstructing stock from map time. See [Neutral Shops And Mercenary Camps](neutral-shops.md).
Version 29 adds each neutral-shop stock entry's effective `maximum`. Runtime `AddItemToStock` / `AddUnitToStock` overrides can differ
from the object-data maximum, so both item merchants and Mercenary Camps restore that explicit cap instead of recomputing it from SLK
or map object data. The current count and absolute replenishment deadline continue to restore against the saved simulation clock.
Version 30 persists the fixed multiboard, multiboard-item, and texttag registries (including `texttag.unit` via `F_EDICT`) so JASS
scoreboard/floating-text handles relocate by slot index. See [Multiboard And TextTag](multiboard-and-texttag.md).
Version 31 adds the fixed hashtable registry (`level.hashtables[MAX_HASHTABLES]`) plus a typed nested-handle entry payload written
after edicts (not after groups). Nested `unit`/`item` slots call `G_LoadJassHandle` only once restored edict `inuse` bits exist;
`SV_Map` runs `main()` first, so a pre-edict resolve would see the map baseline and drop script-created units. Each `HT_HANDLE`
slot stores the JASS type name; nested types without a host domain (`location`, `lightning`, `image`, `ubersplat`, …) restore as
null with a one-shot stderr diagnostic. JSVM snapshot format remains version 4 — hashtable globals relocate through the host codec,
not an owned-payload allowlist entry. See [DotA Custom-Map Playability](dota-map-playability.md#hashtable-saveload).
Version 31 also persists the Human construction `build_preview` edict reference, so an accepted build order's owner-only
Construction Site Indicator survives save/load as an entity-index fixup rather than a process pointer. Older saves are rejected
by the exact-version guard.
Version 32 persists whether the map's `config()` phase completed before `main()`, so a restored map does not rerun that setup phase
or lose the lifecycle state that gates authored player/team/color initialization. It also adds the WC3-owned mutable Blight cell plane immediately after the level-field stream and adds each unit's `blight_growth` alias/current-radius/next-update state to the raw `edict_t` contract. Load requires the saved Blight byte count to exactly match the freshly initialized map Blight grid and restores it before edicts/JASS state, so `SetBlight*` changes and `Abli` expansion progress survive without being reconstructed from nearby buildings. See [Blight](blight.md). Version 31 saves are rejected by the exact-version guard.

Version 32 also adds `edict_s.permanent_invisibility_reveal_until`, the authoritative server-time deadline used by `Apiv` Permanent Invisibility after spawn, attack, or spell-cast reveal. The field is serialized explicitly in `edict_fields[]`; the expanded `edict_t` size and version reject older layouts rather than interpreting shifted entity state.

Version 33 adds the level-owned Warcraft lightning presentation registry (`next_lightning_id` plus active `LIGHTNINGEFFECT` records). Source/target positions, rawcode, colour, start time and optional expiration survive a save, so a live Chain Lightning bolt does not disappear or restart its lifetime merely because the game was reloaded.

Version 34 adds the `lightning` JASS handle domain. `AddLightning` and `AddLightningEx` handles now point at the same stable lightning registry slots used by ability presentation, so movement, colour, destruction, hashtable entries, and active script globals preserve their identity across save/load.

Version 35 adds entity attachment and spawn-generation fields to each lightning registry slot. Ability bolts save their source/target edict indexes and spawn times, then restore those references through the normal `F_EDICT` fixup path. Each outgoing datagram refreshes attached endpoints from the current unit origins; if an edict was freed or its spawn generation changed, that endpoint is detached instead of following a reused slot. The saved coordinates remain available for explicitly positioned JASS lightning.

Version 36 expands the raw `GAMECLIENT` Warcraft music state with per-client session serials/IDs and the thematic restore descriptor. The session ID is the authoritative lifecycle identity echoed by client `music_selected` / `music_finished` acknowledgements, replacing playlist-text/index hashes that could disagree after random initial selection, missing-track fallback, or comma/semicolon normalization. The restore descriptor persists the interrupted source, selected track, pause state, and the client-observed audible millisecond snapshot reported when thematic music begins, so a save taken during a theme can restore the underlying ordinary music session after reload. Version 35 saves are rejected by the exact-version guard because the raw client record grew.

Version 37 adds the timer callback generation/pending fields, expands the bounded event ring, and adds the Undead Sacrifice queue relationship. A queued Shade carries `edict_s.sacrifice` and `sacrifice.worker` is relocated through an `F_EDICT` fixup; the saved worker generation and hidden/paused restoration scalars remain in the raw edict record. This lets a save taken while an Acolyte is hidden inside a Sacrificial Pit resume the same production item and cancel/complete safely instead of retaining a process pointer. Version 36 saves are rejected by the exact-version guard because the timer schema and event ring layout changed. See [Undead Sacrifice](sacrifice.md).

Save format version 45 adds Way Gate destination/activation values and explicit approach state. `movement.waygate_target` and `movement.waygate_goal` use the ordinary `F_EDICT` relocation contract, `waygate_target_spawn_time` guards the target incarnation, and the behavior continues to use the existing `currentmove` `F_MMOVE` relocation. The exact version guard rejects version 44 saves independently of the `sizeof(edict_t)` header check. See [Way Gates](way-gates.md).

Ability-owned temporary summons rely on three already-persisted parts of the edict contract together: the `owner` `F_EDICT` fixup, the scalar `summon_ability` alias, and the owned sparse timed-status record carrying `BTLF` (`timestamp`/`duration_ms`). The save regression suite covers that combined lifecycle so a loaded temporary summon keeps both provenance and its remaining timed-life state; no additional save-format field is required.

Corpse lifecycle (`AI_CORPSE_UNRAISABLE`, `AI_CORPSE_NO_DECAY`, `AI_CORPSE_RESERVED`, and `AI_CORPSE_IN_CARGO`) rides in the already-persisted `aiflags` edict field, while the existing persisted `cargo.units[]`/`cargo.count` links the Meat Wagon to the actual stored corpse edicts. Thus cargo occupancy, corpse identity, decay move/timer, and active corpse-consumer reservation survive save/load without a new format field. Graveyard `Agyd` production uses an ordinary owner-linked thinker plus `freetime`; `graveyard_think` is in the saved callback roster, so its cadence resumes without a format change. See [Corpse Lifecycle, Cannibalize, and Raise Dead](corpse-mechanics.md).


Groups use reusable stable ordinals in a growable pointer table: `level.num_groups` is the high-water mark while `level.group_capacity` is transient allocation capacity. Each `ggroup_t` is separately allocated so growing the pointer table never moves a live handle. `DestroyGroup` releases an ordinal for later reuse; `GroupClear` only clears membership. Live JASS group handles serialize as stable ordinal indexes. See [JASS Groups](jass-groups.md).

Groups, timers, triggers, and event handlers may grow after `main()`. The header accepts a save that has *at least* as many of those objects as the freshly initialized map, then `RestoreRegistrySlots()` allocates the extras. A live count higher than the save still rejects. Quests remain an exact match because they are restored in place.

The current format is process-independent for entity relationships: `F_EDICT` fields and camera targets are written as entity indexes and resolved back to `g_edicts[index]` by `ReadGame()`. Before raw edict records replace the freshly loaded map baseline, `ReadGame()` clears the baseline spatial tree and then links each restored entity exactly once. Client pointers are restored from player slots, player names from inline JASS name storage, and map-player rows from the loaded map plus `PLAYER.number`. Malformed headers, truncated records, and entity indexes reject the load; client pointers are never read from the file as addresses.

Groups, triggers, timers, and events use deterministic handle ordinals. Group objects are dynamically allocated behind a growable pointer table; membership is stored as entity indexes and each serialized record persists `inuse` so destroyed holes remain distinguishable from live empty groups. Each trigger stores its disabled flag plus action/condition function names so a trigger created after `main()` still has its callbacks after load. Timers preserve their handler name, duration, remaining time, periodic/paused/running flags, and resume relative to the load time. Timer callbacks and timer-expire trigger actions enter the normal coroutine queue and retain `GetExpiredTimer()` context.

Weather effects use the same stable-slot rule. `level.weather_effects[MAX_WEATHER_EFFECTS]`, `next_weather_id`, each slot's rawcode, rectangle, enabled flag, and renderer handle ID are serialized as level state. A non-null JASS `weathereffect` snapshots as its fixed slot index, so globals keep pointer identity across load. `ReadGame()` restores the registry before the JASS snapshot and replays it to connected clients; reconnecting clients also receive the same full weather sync from `G_ClientBegin()`. See [Weather](weather.md).

Event handler registrations store type, subject entity index, trigger index, timer index, region, range, game-state ID, `limitop`, and limit value. The extra condition fields preserve `TriggerRegisterGameStateEvent` time-of-day registrations across save/load. The unread portion of the bounded gameplay event ring preserves event type, subject/source entity indexes, scalar value, optional point target, and the target registration ordinal. Consumed queue entries are not saved. Loads reject queue overflow and unresolved entity or registration IDs.


## JASS Snapshot

Save92/JSVM8 reject programs compiled with the former right-associated arithmetic
and host JASS scalar operators. Chained expressions now retain left association,
software scalar operations and truncating promotion. Token ordinals/operator
identities are rebuilt from source; old continuations cannot be reused. Eight
expression-authored Move continuations verify callback and movement state before
and after order admission. See [compiled expression parity](retail-pathfinding-engine.md#compiled-expressions-retain-retail-arithmetic-and-evaluation-order).

The embedded snapshot starts with `JSVM`, snapshot format version 9, a program-identity hash, mutable-global count, and sleeping-coroutine count. It stores:

- mutable scalar globals and sparse array entries;
- integer, real, boolean, string, code, null, and supported typed-handle values;
- shared-handle identity by stable native-domain ID;
- VM-owned handle identity and bounded payloads for sounds, camera setups, rects, locations, forces, game caches, regions, fog modifiers, and converted value objects;
- `boolexpr`, `conditionfunc`, and `filterfunc` handles by semantic JASS function name;
- sleeping coroutine frames as function/block token ordinals, locals, operand stack values, wake delay, and event context, including scalar and optional point spell response data.

The snapshot never writes parser pointers, dictionary links, refcount addresses, stack pointers, or `jmp_buf`. Code values and coroutine PCs resolve against the already-parsed program after the identity hash matches. Handles relocate through game-owned codecs for entities (`unit`, `widget`, `destructable`, `item`, `effect`), players, quests, quest items, events, triggers, groups, timers, timer dialogs, leaderboards, multiboards, texttags, hashtables, and weather effects. Safe VM-owned handles serialize their payload and snapshot-local identity so aliases remain aliases after load. Unsupported non-null handle types reject the save with a diagnostic instead of writing an address or silently dropping the value.

Handle encoding dispatches value handles, VM-owned payloads, and function handles before consulting the game host. A failed host lookup means null only for host-owned native domains, such as a removed unit; applying that rule to VM-owned handles would silently replace valid sounds, camera setups, rects, locations, forces, and game caches with null.

JASS sound handle payloads remain part of the VM snapshot, but one-shot presentation parameters currently maintained by the game host (`SetSoundVolume`, `SetSoundPosition`, and `AttachSoundToUnit`) are transient. `ReadGame()` clears that host-side table before reconstructed sound handles can reuse an old pointer value. Scripts that need those presentation parameters after restore must set them again before the next `StartSound`; continuous playback state is not yet serialized.

Point-order waypoints initially reserve 256 classless, collisionless,
`SVF_NOCLIENT` edicts before map entities. Allocation leases unused destinations;
when the available batch runs out, it traces live actors' destination references
and transitive `secondarygoal` chains before reclaiming storage. If every managed
destination is referenced, it allocates another 128 ordinary edicts. A live order
must never lose its destination to circular reuse. `SVF_MOVE_WAYPOINT` records
membership in the saved `svflags`. Format101 requires this ownership marker and
rejects older versions instead of reconstructing legacy destination membership.
The reserve's base, count and cursor remain serialized. Additional destinations and all `goalentity`/chain
references use ordinary `F_EDICT` index relocation. Leases and tracing bitsets are
derived state rebuilt after load. The 304-member public-order regression checks
retained destinations, saved continuation and bounded reuse after Stop.

The event ring now accommodates 32,768 pending records, enough for both issued-order
notifications for every supported world entity before dispatch. `F_STRUCT_RING`
writes only an unread count followed by its mapped records, so changing runtime
capacity retains the mapped record layout. Format101 covers the expanded queue
contract and marked destination ownership; older versions are rejected. A regression
publishes 4,096 point events across the ring boundary and verifies payloads,
entity relocation and FIFO order after load. Food and Shadow Meld component
capacity likewise follows the world entity bound; their existing sparse pool
serialization is unchanged.

Saving is allowed only at a VM safe point. `jass_writesnapshot()` rejects a request while a synchronous JASS call or coroutine is actively executing. Yielded `TriggerSleepAction` coroutines are safe and resume from their saved semantic PC after load.

## Field Table

`games/warcraft-3/game/g_save.c` keeps the `field_t fields[]` table synchronized with `struct edict_s` in `g_local.h`. Fixed-size
`edict_t` and `GAMECLIENT` records are still copied as one block. Embedded non-pointer state such as
`abilitycooldowns[]` (cooldown rawcode/start/end) and scalar animation state therefore round-trip with that raw record
and need no `field_t` entry. Animation requests and properties are immutable references: format107 excludes their
pointers and writes bounded logical text separately. The adjacent `runtime_fields[]` and
`client_runtime_fields[]` tables describe the process-owned bytes that must be zeroed before that copy. This keeps the
common path memcpy-shaped while making pointer exceptions declarative rather than a hand-maintained assignment list.

Status slots use the same rule in their optional pool record: scalar timing,
rank and payload fields round-trip in the record, while the applying `source`
is a nested `F_EDICT` field in `abilstatus->slots`. The edict's storage pointer
is runtime state reconstructed by the pool stream.

`EDICTFIELD(x, type)` describes one scalar field with `array_size == 0`. `EDICTFIELD(x, type, count)` describes a contiguous array from the base offset; the serializer walks `count` elements using the field type's element size. For example, the six inventory pointers use `EDICTFIELD(inventory, F_EDICT, MAX_INVENTORY)` rather than six duplicate descriptors.

- Add every persistent `edict_t` entity pointer to `fields[]` as `F_EDICT`, including array elements and nested fields.
- Use `TFC(type, field, kind, capacity, count_field)` for a bounded typedef-backed array. The descriptor writes and restores `count_field` itself, then processes that many elements; do not map the count separately.
- Persistent movement defaults are part of that rule: Attack-Move/Patrol waypoints and `movement.follow_target` must be encoded as entity indexes rather than raw pointers.
- Do not add process-owned pointers such as path textures, metadata rows, or animations. `WriteEdict()` clears `FIELD_RUNTIME` pointers and `ReadEdict()` rebinds class metadata; spatial links are rebuilt with `gi.LinkEntity`.
- Edict C callbacks (`think`, `stand`, `birth`, `prethink`, `die`, `idle`, `move`, `run`, `attack`, `pain`) use `F_CFUNCTION`, not `F_IGNORE`. Add every production assignment to the versioned `save_cfunctions[]` roster in `g_save.c`; an unrostered pointer fails the save instead of writing an address.
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

The format does not yet snapshot gameplay fog-of-war grids, bot runtime, alliances, or cinematic filter. Environmental distance-fog state is serialized since version 16. Client message storage is part of `GAMECLIENT`, but transient presentation lifetimes are not reconstructed. Edict C callbacks persist through `F_CFUNCTION` (see [C Callbacks](#c-callbacks_f_cfunction)); the active `umove_t` is restored by `F_MMOVE` relocation (see [Active Behavior](#active-behavior_f_mmove)). Menu callbacks are code pointers and are reset on load; restoring an active targeting/build submenu requires a semantic menu-state enum rather than raw function addresses. There is no backwards-compatible reader for v12 or earlier saves. Version 9 packs C-callback roster indexes into the edict blob that version 8 zeroed and rebound from class data. Version 10 adds the fixed weather-effect registry, weather handle IDs, the `weathereffect` JASS handle codec, and false-time state to the level stream; after load the authoritative weather set is replayed to connected clients. Version 11 adds per-slot JASS group lifecycle state so destroyed group slots can be safely recycled and restored. Version 12 expands `GAMECLIENT` with semantic music state; the client decoder itself is not serialized, so music resumes from the last JASS-defined start/seek position rather than an exact continuously advancing decoder head. Version 13 serializes the dynamically sized JASS group registry outside the fixed level-field schema while preserving group-handle ordinals. The current music session fields expand the raw `GAMECLIENT` record and therefore use save format version 36; version 35 saves are rejected by the exact-version guard.

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

JASS timers retain `duration`, a `remaining` countdown and its `updated` simulation-millisecond
cursor. `G_RunTimers` consumes `level.time - updated` once; repeated drains at the same clock
cannot age a positive-duration timer. Pause materializes elapsed time; Resume and Start publish
a fresh cursor. GetRemaining reports unconsumed elapsed time without modifying the timer.
Save72 writes the cursor through `timer_fields[]` alongside the saved server clock. Load restores
both together, with no per-timer rebase or compatibility conversion. Older formats are rejected.
The prior fixed100ms decrement was incorrect once RunFrame drained at each5ms primary quantum.
See [normal timer admission](retail-pathfinding-engine.md#public-timer-admission-reaches-move-from-zero).

Regression test: `wc3_save.load_restores_server_clock_onto_saved_time` in
`games/warcraft-3/game/tests/t_game.c`.

## Hero walk / save / load

The serializer already round-trips edict pointers, Hero skill slots, runtime
ability lists, and inventory items. The gameplay contract a player notices is
stronger: a Hero that has walked away from spawn must restore that location
together with learned skills and carried items.

CI covers that contract without retail maps in
`wc3_save.walking_hero_round_trips_abilities_inventory_origin`
(`games/warcraft-3/game/tests/t_hero_saveload.c`). The test learns two Paladin
skills, inserts two charged items, issues `unit_issueorder(..., "move", ...)`,
steps until origin changes while `currentmove` is still `walk`, then
`WriteGame`/`ReadGame` and checks origin, `heroabilities[]`, `abilities.added[]`,
inventory class/charges, and that one more walk think still advances.
`wc3_save.hero_audit_*` drives `G_HeroSaveLoadAuditFrame` for hidden-first-Hero,
paused/cinematic wait, and dest-snap retries.

Campaign maps are a local diagnostic, not `make test`. Arm the production
binary with `wc3_hero_saveload_audit=1`; after `G_LoadMap` the game module waits
for a live Hero, issues a short walk, saves, reloads through `WriteGame`/
`ReadGame`, and prints `HERO_SAVELOAD` lines. Isolated per-map processes:

```sh
make audit-wc3-hero-saveload
make audit-wc3-hero-saveload WC3_HERO_SAVELOAD_ARGS='--map Human02.w3m --jobs 1'
make test-wc3-hero-saveload-audit
```

Reports land in `build/wc3-hero-saveload-audit/`. `no_hero` is typical for
credits and some Rexxar sub-areas; it is not a serializer failure. This path
does not call `SV_Map` reconnect; it restores onto the already-loaded map, which
is the same game-module boundary the CI test uses.

The walker must not start during intro cinematic. HumanX03 (Hkal dungeon
pocket), NightElfX01 (Maiev intro start), and OrcX03a (Rexxar intro cliff)
used to issue `move` before Intro_Cleanup teleported the player Hero.
HumanX06Finale picked hidden Kil'jaeden `N000` first. The audit now skips
`RF_HIDDEN`, waits for `!paused` and non-cinematic UI (with a timeout fallback
for finales that stay cinematic), and re-issues if cleanup teleports mid-walk.

`unit_issueorder("move")` returning true does not mean the snapped dest left
the cell; ClosestPathable can fold an 80-unit click back onto the stand-in
cell, then the first think arrives immediately (`stand`, origin unchanged).
The walker retries 80/160/256/512 and eight directions until `goalentity` XY
actually leaves.

Do not teach `skip_cutscene` to unpause, fire ESC skip, or end cinematic; JASS
cleanup owns that. Close-note: tracked by
[#438](https://github.com/corepunch/open-realm/issues/438).

Manual cheats (`sv_cheats 1`): `hero dump` prints the same snapshot line, and
`hero walk [dx dy]` issues a move order from the selected Hero. See
[Campaign Map Audit](map-audit.md) for the sibling startup smoke sweep.

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

The roster indexes and callback names are part of the format. Bump the save version if their identity changes; keep the roster's names aligned with the current implementation instead of retaining old-name aliases. Production assignments retained
by version 10:
`monster_think`, `blight_mine_think`, `G_FreeEdict`, `G_EffectThink`, `G_EffectValidateTarget`,
`blizzard_think`, `flame_strike_tick`, `siphon_mana_think`, `unit_stand`/`unit_birth`/`unit_die`,
and `tree_stand`/`tree_birth`/`tree_pain`/`tree_die`. `idle`/`move`/`run`/`attack` have no
production assignments yet; they still go through `F_CFUNCTION` so a later assignment must be
rostered.

The shared targeted-spell approach callback `S_SpellTargetApproachThink` is also in the roster.
`wc3_items.soul_gem_pending_approach_round_trips_save` verifies that a Soul Gem cast waiting to
walk into range preserves both this callback and its originating item across save/load. Point-target
spells use the same callback and retain their clicked destination while walking into range.

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

Ability helper thinkers may also use the existing `channel_t` pool purely as a save-safe incarnation carrier.
Reincarnation snapshots its Hero owner, Acid Bomb snapshots both caster and victim, Flare snapshots its caster, and the
timed Metamorphosis reversion snapshots the transformed unit before using delayed callbacks. `reincarnation_think`,
`acid_bomb_think`, and `morph_end` are appended to the C-callback roster so saves made while those effects are pending
restore the live callback rather than failing on an unrecognized process pointer. This
does not make those abilities channels and adds no save-schema fields: the existing `owner`/`goalentity` `F_EDICT` links
and `channel->owner_spawn_time` / `channel->target_spawn_time` scalars carry the contract.

Save format version 39 adds the launch-time attack type for basic projectiles.
Version 38 added the launch-time artillery attack type, target masks, splash
radii, and damage factors so in-flight shots retain their impact profile across
save/load.

Save format version 40 adds the fixed region registry and region references in
event registrations. Region handles use stable registry slot IDs in the level
state and JASS snapshot format 6 persists `GetTriggeringRegion()` in yielded
trigger context. Version 41 adds persisted region and region-event handle
generations so recycled slots retain their `GetHandleId` identity after load.
Version 42 persists the edict incarnation captured by unit-bound event
registrations, preventing slot reuse from retargeting an old registration.

Version 43 persists the queued-event subject incarnation (`edict_spawn_time`
and `edict_spawn_tracked`) alongside its edict index, so an unread event is
discarded if its subject slot has been freed or reused. Version 44 persists the
queued-event source incarnation (`source_spawn_time` and
`source_spawn_tracked`) alongside its edict index, allowing dispatch to clear a
stale source without losing an event for a still-current subject.

Version 45 adds per-edict Way Gate destination/activation values plus the
explicit approach target, approach goal, and target-incarnation guard. The two
entity pointers use `F_EDICT` relocation and the live Way Gate move uses the
existing `F_MMOVE` relocation. Version 44 saves are rejected by the exact-version
guard; the `sizeof(edict_t)` header check remains an independent layout check.

Version 46 adds unit-specific forced-visibility reference counts used by
Soul Trap, plus the completed Soul Trap item links. Version 45 saves are
rejected by the exact-version guard; the expanded `edict_t` size is checked
separately before decoding the raw edict records.

Version 48 adds the Entangled Gold Mine overlay's caster relationship.
`mineoverlay.caster` uses the normal `F_EDICT` relocation path, while the saved
caster spawn generation and Entangle ability alias let load restore the same
per-unit hidden/permanent Aent lifecycle without trusting a recycled entity
slot. Version 47 saves are rejected by the exact-version guard; the expanded
`edict_t` size remains an independent layout check.

Version 49 persists whether Aent was already permanent before an Entangle
overlay made it permanent. The final live overlay restores that original value,
including after save/load; version 48 saves are rejected by the exact-version
guard.

Version 50 persists the generation-guarded Tree owner on an Entangled Mine
overlay, alongside the caster relationship. Direct Tree removal can therefore
find and tear down the exact overlay after load without relying on proximity;
version 49 saves are rejected by the exact-version guard.

Version 51 persists Ancient Root/Uproot transition state. Version 50 saves are
rejected by the exact-version guard.

### Cursor target presentation

Version 52 adds a generation to the transient held-item reference in the client
menu. Both fields are cleared by the runtime-field schema, along with the target
callbacks. ReadClient clears cursor mode/image stats; connected clients resolve
their restored race/CustomSkin model against the rebuilt configstrings. A save
made while carrying an item on the pointer cannot restore a stale pointer or icon.
The `wc3_cursor.save_discards_transient_held_item` regression covers this inverse.

Cursor signal overlays and minimap point-routing flags are transient and clear
on load (version 52 client layout). They never serialize an active input overlay.

Version 53 persists each unit's explicit `UnitShareVision` recipient mask.
Version 52 saves are rejected by the exact-version guard.

Version 54 preserves explicit scripted turn-speed/window overrides and the
pre-turn translation decision. These primitive fields change edict offsets;
version 53 saves are rejected. The native-setter/steering save regression is
`wc3_movement.scripted_turn_state_survives_save_load`; see
[numerical movement integration](retail-pathfinding-engine.md).

Format55 adds the committed Move XY velocity. It survives the raw edict save image; stopping/reset clears it in Move. Format54 is rejected because subsequent edict field offsets differ. The alternating-heading movement regression compares uninterrupted and resumed position/velocity words exactly.

Version56 adds Move cohort identity and its level allocation cursor. Active selection groups retain membership and recompute survivor speed after load; a post-load Stop regression verifies the cap changes. The exact-version guard rejects55 and older saves. See [active Move cohort speed](retail-pathfinding-engine.md#active-move-cohort-speed).

Version57 adds the explicit per-unit `current_order_id` (`F_INT`) used by
ordinary point Move/Smart, separately from the pending FIFO and issued events.
The active head survives active-plus-queued save/load; Stop and natural arrival
retire it, and edict reuse starts at zero. Selection groups also retain their
active Move command. Version56 and older records are rejected. Remaining
command owners are ORDER-01.6; see
[current point orders](retail-pathfinding-engine.md#current-point-order-ownership).


The September30 upstream sync advances the outer format to **58**: the merged
raw unit layout combines committed retail Move velocity/current-order state with
explicit allied attack intent, and the camera field schema includes upstream
orientation/noise state. Both parents' earlier layouts are rejected. Existing
ROC/TFT round-trips cover live movement, allied intent and camera state; the
previous-format rejection matrix includes versions39 through57.

The subsequent September30 upstream sync advances the outer format to **59**.
Camera noise parameters now live in `client.camera.noise[]`; `client.ps` retains
only the game-evaluated `viewoffset`/`eyeoffset` samples. Both records use the
existing raw `F_STRUCT` schema, so their changed layouts require rejecting58
independently of the edict-size guard. ROC/TFT round-trips retain the parameters
and all six sample components after clearing live state. The prior-version
matrix now rejects39 through58; JSVM remains7. The network contract is the
upstream-approved generic view-offset protocol; this merge adds no wire fields.

Version63 persists Move primary-clock time/epoch/span, the5ms cursor and six-step callback phase, pending owner dispatch, exact sampled/committed fine pose and prediction time origin. Runtime dispatch flags and per-frame callback stamps are cleared on load. Save62 is rejected. See [the clock integration and restoration evidence](retail-pathfinding-engine.md#primary-clock-reaches-move-and-predicted-positions).

Public `SetUnitPosition/Loc` clear active Move/Patrol, queued orders and group
state before placement. A save immediately afterward retains the exact fine
pose and remains stationary after load; no additional fields or format change
are needed. See [forced-position restoration](retail-pathfinding-engine.md#forced-position-stop-reaches-the-engine).

Version64 adds a counted byte stream of mutable terrain pathing immediately
before Blight. It preserves native `SetTerrainPathable` changes separately from
static entity footprints and transient dynamic occupancy. Load validates the
byte count against the reloaded map, restores terrain and Blight, then bakes
static footprints after entity reconstruction. The exact-version guard rejects
63 and earlier. Edict, callback and JSVM7 layouts remain unchanged. See
[terrain natives](retail-pathfinding-engine.md#terrain-pathing-natives-reach-the-engine).

Version65 adds the two pathfinding-owner PRNG words to `level_fields`.
Public `SetRandomSeed`/`GetRandomInt`/`GetRandomReal` use this state; saved
continuation is tested through the compiled natives. Formats64 and earlier
are rejected. Edict, callback and JSVM7 layouts remain unchanged. See
[deterministic owner random state](retail-pathfinding-engine.md#deterministic-owner-random-state-reaches-public-natives).

Version66 adds Move-owned authored repulsion: pending vector, packed
configuration/category/rank/cooldown, active membership and relocated next
pointers, plus the level list head and alternating owner phase. An actual idle
overlap save reproduces subsequent positions, vectors, cooldown and random
state exactly after load. Formats65 and earlier are rejected; JSVM7 and the
callback roster are unchanged. See [repulsion integration](retail-pathfinding-engine.md#authored-repulsion-reaches-idle-engine-units).


### Retained adaptive routes (version68)

Move retains accelerator-space points, count/index, native goal and radius beside
its fine curve. The edict tail writes fine points followed by adaptive points;
readers rebuild both allocations and reject invalid count/index, nonfinite and
truncated data. Process pointers and the world bake revision are runtime fields.
After the saved terrain and obstacles are rebuilt, restored coarse routes bind
to the new world's bake revision. Exact-version checks reject earlier layouts.
The public long Move scheduler regression compares180 continuation frames after
loading, with retained coarse count/index; JASS snapshot7 and wire data are
unchanged. See [the engine integration](retail-pathfinding-engine.md#retained-coarse-progress-refills-the-fine-route).


### Moving-blocker waits (version69)

Move persists the unsigned eligible-advance countdown and its blocker edict
reference. The movement schema uses `F_INT` and `F_EDICT`; save/load converts the
pointer to/from an edict index and rejects invalid pointers. A new order clears
both values. Actor reclamation clears other actors' references before slot reuse
while retaining their remaining waits. Public `RemoveUnit` defers reclamation;
the identity clears at the Move removal callback. A public two-Move fixture saves
with a live4-advance wait, removes its blocker, consumes all four waits despite
query0, then resumes with an exact48-word continuation after load. See
[the original policy and integration](retail-pathfinding-engine.md#ordered-moving-waits-reach-ordinary-move).


### Current combat and cargo state

Format71 writes the current entity struct directly through the normal Quake II field serializer. Attack target incarnation, cooldown/backswing deadlines, per-weapon backswing points and range buffers, and pending Cargo Drop state live in that record. The cargo goal uses the ordinary `F_EDICT` fixup; its initiating rawcode and goal spawn identity remain scalar state. Invalid external pointers on write or unallocated goal indexes on load are rejected.

All format-56 variants, including the former combat/cargo extensions and extensionless files, are rejected. There is no frozen entity projection, optional extension reader, or legacy propulsion-window migration. Explicit zero propulsion windows round-trip as authored runtime state.

Attack cooldown begins at swing start, independently of animation `wait`; it continues elapsing when another order or rooted state pauses the attack callback. A target escaping before damage point cancels the pending hit but does not erase the cooldown. A committed melee hit or projectile launch starts backswing recovery, and a save/load during recovery preserves only the remaining backswing time.

Regression coverage includes prior/future version rejection, map-selection layout rejection, current combat/cargo round-trips, invalid cargo goal references, and payloads with unexpected trailing records. The movement suite saves a Zeppelin en route and completes its unload after restoration. Run `make test-wc3-engine WC3_PATTERN='wc3_save.*'` for both ROC and TFT.


### Upstream combat/cargo merge (version70)

The October1 merge combines format69's verified Move state with upstream's
current combat and cargo fields. Both parent layouts are rejected, including
prior69 and upstream57. The current raw record, recursive pointer fixups and
owned route tails stay authoritative; no legacy projection or optional extension
is introduced. Movement retains software scalar normalization, explicit zero
window flags and the original pre-turn propulsion decision. Upstream's animation
policy uses that existing decision to retain Stand while blocked and resume Walk
when admitted, avoiding a second host-float/post-turn gate.

Payoff31's countdown heading consumes the existing retained native prediction;
it changes no field meaning or layout. Payoff31 retained save70; payoff32 advances the current format to71. The public
oblique-wait regression saves before four held ticks and repeats120 exact
position/native pose/velocity/facing/heading/wait/order words after load, then
proves that ordinary Move resumes.


### Fine retry state (version71)

`edictMovement_s.retry_count` stores original path98 through the normal `F_INT`
field entry. Save71 rejects prior70 and all older layouts. Peer20 fine retry
saves count6/7, fine count0/indexUINT32_MAX, retained coarse points/index and
peer wait identity/delay. The public JASS order test restores that live state,
refills through the real thinker and repeats120 ticks/1440 state words including
the owner random state. A focused field round-trip and prior-version rejection
run in ROC/TFT. Network and JASS snapshot formats remain unchanged.

Public ground creation now retains the original freshly committed native fine
pose rather than reconstructing it from requested/published world XY. Save71
already owns that representation, so no layout, callback or meaning changes.
The [spawn regression](retail-pathfinding-engine.md#public-spawn-admission-and-initial-mover-pose)
saves the fractional public getter result immediately and repeats640 scheduler
continuation words through normal ReadGame/WriteGame.

### Timer countdown cursor (version72)

The new `gtimer_s.updated` field is ordinary authoritative timer state. It preserves
elapsed time that has not yet been materialized into `remaining`, including a save
between timer drains. Tests restore a callback-restarted timer with45ms unconsumed
elapsed time, reject71 and all older listed versions, and resume public timer-driven
Move through the fractional actor and subsequent RemoveUnit/CreateUnit lifetimes.
The JASS snapshot remains7; no network fields or callback identities change.
Original scalar heap deadlines, arbitrary short/zero periods and timer-clock epoch
rebasing remain explicit numerical backlog work.

### Singleton group route (version73)

Move persists its singleton group adaptive chain independently of the member's
adaptive/fine buffers. Points, count/index, final native destination and radius
survive; the process-local bake revision is rebound after terrain/entity
restoration. Two public oblique saves resume260 original movement commits,
including a save immediately after intermediate arrival with both member
buffers empty and the final public order still active. Version73 initially rejected version72 and earlier; the current version75
reader also rejects version73; there is no migration. Group tail
extent/index, finite coordinates and complete bytes are checked before use.
See [retail group-destination payoff](retail-pathfinding-engine.md#public-oblique-move-retains-the-singleton-group-destination).


### Physical Move groups (version74)

Move's physical owners are independent of JASS collection handles. Save74 retains
active owner identity, public goal, selected native formation point/heading,
route buffers and counted member rows through `move_group_fields`. Member unit
pointers use `F_EDICT` indices and saved spawn generations. Pool pointers,
capacity, ticking and bake revisions are process-owned; loading reconstructs
owners after edicts, rejects invalid/duplicate owners and members, then rebinds
route revisions after the world bake. Map replacement/shutdown releases owners.
Creation-order links and allocation slots are derived runtime fields. Loading
sorts by saved sequence once; ordinary allocation prepends new owners and
retirement unlinks them. Each owner pass snapshots pointer/sequence pairs so
callbacks cannot make a reused or newly created group run in the old visit.
The request-ID upper bound is also reconstructed from active, previous and
queued identities; counter wrap retains authoritative collision checks. These
runtime indexes do not add serialized fields or change edict size.
Partial records clear runtime addresses before error cleanup. The same bounded,
finite three-buffer payload helpers serve edict and group routes.

A public two-unit group resumes87 exact original commits from saves during
travel and the final partial-route retry. Invalid group counts, owner identity,
references, generation, duplicate members, nonfinite destinations/points,
route extents/indices and truncated tails are covered in RoC/TFT. There is no
compatibility path for version73. See [shared Move payoff](retail-pathfinding-engine.md#public-pair-movement-uses-a-shared-move-owner).

An ordinary owner with zero members remains valid until its next Move visit,
including across save/load. `wc3_save.rejects_invalid_physical_group_payloads`
checks this as a valid control and clears the restored registry before reading
the next malformed payload. Expecting rejection instead leaves the registry
count live, so the next isolated read overruns its newly allocated pointer array
and crashes during cleanup. The production lifecycle round-trip is covered by
`wc3_movement.public_mover_retirement_cancels_pending_and_active_owners`.


### Pending fine searches (version75)

Move retains the ordinary fine-search FIFO as edict links, with its shared
work/countdown, owner visit counter and each unit's fine-request timestamp.
The owner counter follows original157610/15aa80: starts at0x400, increments
once per owner update and reloads0x400 after unsigned wrap. Fine retries
use original168910's ten-visit gate; denied FIFO requests clear their timestamp.

The serializer maps head/tail and per-unit previous/next links through
`F_EDICT`, validates pointer bounds before traversal, and rejects inconsistent
counts, cycles, disconnected queued units, dead records and malformed boolean
values. Validation runs before writing and after all edicts are restored.
The Save suite verifies FIFO order and interval continuation through actual
`WriteGame`/`ReadGame`, twelve invalid graphs, and rejection of version74.
Broader scheduler pools and accelerated request timestamps remain separate
retail pathfinding work.


Format80 adds physical Follow target edict/generation, refresh countdown and
per-member arrival range. Loading validates target/member ownership, finite
nonnegative ranges and bounded refresh state; all earlier exact versions through
79 are rejected. The moving-target speed-change regression resumes2595 exact
suffix commits from three saves during approach, persistent Follow and target
travel. Destroyed timers release their JASS callback so retired slots cannot
serialize a pointer into a replaced VM program. Unknown saved function names
identify the field and name in the load diagnostic. See
[Smart Follow parity](retail-pathfinding-engine.md#smart-follow-tracks-a-moving-target-through-a-speed-change).

Ground Follow now cancels synchronously when its target dies or is removed,
detaching its physical owner before stand. Actual original target pool/public
handle reuse never silently adopts the replacement. The public death and
removal journeys each match948 normal-frame commits and1746 Save80 suffix
commits, including a save while the replacement exists but Follow is idle.
[Payoff48 evidence](retail-pathfinding-engine.md#follow-cancels-synchronously-before-target-pool-reuse)
records the exact scope; save format80 is unchanged.

Public target axis teleport now keeps a retained point Move through turn waits;
its retail fine arrival/retry policy owns completion instead of legacy near-goal
settling. Four Follow teleport journeys match4091 normal commits and7872 saved
suffix commits. A test-only commit observer records movement before same-clock
JASS writes, and saves use completed-owner boundaries. Save80 is unchanged.
[Payoff49 scope and evidence](retail-pathfinding-engine.md#follow-tracks-public-target-teleports-without-premature-point-settling)
leaves collision resizing open.

Public Chaos now changes unit type and collision in place after its authored
requirements are met. Existing Follow groups retain their range; new nearby
approaches use retail's half predicted edge distance and persistent Follow uses
the resized radii. Five normal journeys match5075 commits and9725 saved suffix
commits, including saves before deferred/research-gated morph. The retained
ability itself owns pending work; removing it cancels the morph, and successful
resolution consumes it. Save format80 is unchanged.
[Payoff50 scope and evidence](retail-pathfinding-engine.md#follow-retains-active-range-and-admits-resized-targets-with-half-edge-approaches)
leaves moving-unit occupancy, other locomotion/body families and exact automatic
morph timing open.


### Deferred type rebind and scalar timer owners (version81)

Save81 retains the Chaos rawcode, pending phase and primary deadline; Move's
pending type-rebind handoff and deadline; public timer raw timeout, scalar timing
flag and deadline; and the separate path-owner scalar deadline/validity flag.
Each deadline preserves time, epoch and span. Restoring these cursors avoids
restarting the delayed type change or reconstructing a periodic deadline from
integer server time. Version80 and all earlier exact layouts are rejected.

Four saves during each public moving-radius journey cover enabled delivery,
commit waiting, first resized motion and late travel. Ordinary continuation
matches254 growth and4026 nine-case matrix suffix commits per RoC/TFT variant.
These tests include future births and radius changes after restoration. General
timer getters, paused scalar remainder, heap ties and epoch-crossing inputs remain
open. See [the motion and deadline evidence](retail-pathfinding-engine.md#moving-radius-changes-retain-point-motion-and-scalar-owner-deadlines).


Group-radius mutation now retains the live group maximum separately from its
cached coarse-route footprint. Public group growth/shrink/removal journeys
resume2207 exact motion commits and2193 owner footprint states from twelve
Save81 checkpoints, including before/after pending Chaos rebind or member
removal. Owner counters and player-row budgets publish before any standalone
movement callback after restoration, through the generic A_OWNER_BEGIN phase.
No format change is required. See [payoff52](retail-pathfinding-engine.md#local-group-maximum-and-retained-route-footprint-have-separate-lifetimes).

### Standalone point forced arrival (version82)

Save82 adds Move-owned `point_forced_arrival`, corresponding to original mover
D8 bit10000 after terminal point retry4. It overrides the distance test while
retaining the final angular gate; replacing/leaving Move clears it. Four
checkpoints before partial search, after retry1, after force and during the
final turn reproduce all46 retail suffix commits and normal order completion
per RoC/TFT variant. Old versions including81 are rejected. See [blocked point
goals](retail-pathfinding-engine.md#blocked-point-goals-retain-the-click-through-retry-and-forced-arrival).


Ordinary outside-map point Move reuses Save82's retained public waypoint,
clipped group routing goal, route buffers and forced-arrival flag. Four
checkpoints reproduce47 original outside-west suffix commits per variant,
with Stop and replacement clearing force after restoration. No serialized
layout changes are introduced. See [outside point goals](retail-pathfinding-engine.md#outside-point-goals-clip-routing-while-retaining-the-public-click).

Version83 retains Move's stationary captain membership callback phase, world
home and mapped virtual-actor task reference. The hidden category2/radius0
actor is a real server edict with persisted logical captain ownership.
Recreating captains releases that ownership while outstanding physical tasks
retain the old actor; Stop/replacement/removal release their references.
These records do not serialize the bot JASS VM or its wider AI roster policy.
Both178/250 native journeys have eight saved continuations each, including
private point admission, final retry and forced arrival. Older versions are
rejected. See [stationary captain callbacks](retail-pathfinding-engine.md#stationary-captain-range-callback-and-zero-radius-occupancy).


Version84 adds stationary captain roster cardinality, per-member recruitment
index and entered-state fields. Two independent target-follow owners can
restore before their all-entered shared point handoff; group validation accepts
a model-free captain target only through the member's active retained reference.
Eight pair checkpoints reproduce1648 native suffix commits, including a saved
one-member survivor after the peer completes; bot-free restore adds355 commits.
Stop detaches physical owners synchronously so immediate saves are valid.
Duplicate/out-of-range member indices, invalid roster/entry state and previous
format83 are rejected. The bot VM and wider logical roster remain process-owned.
See [stationary pair integration](retail-pathfinding-engine.md#stationary-captain-pair-private-followers-to-shared-arrival).


Save84 also resumes the [mixed and blocked captain pairs](retail-pathfinding-engine.md#mixed-captain-pairs-and-blocked-home-retries).
The mixed journey resumes2003 exact saved commits; blocked-home resumes2685,
including bot-free restore, the first randomized retry, both naturally forced
arrivals and the surviving peer. Actor placement and its actual listener center
are restored independently of the authored shared point destination. No new
snapshot fields are required for temporary coarse admission exclusions.

Version86 adds Move's shared parameter pool, stable shared-owner counter, each
physical group's shared binding and classification cooldown. The pool is written
after edicts and before physical groups, so bindings resolve during restoration.
Published speed, the pending minimum speed and the live maximum radius remain
separate from the physical path's cached footprint. Loading checks unique IDs,
finite nonnegative scalars, exact bound-group reference counts and the cooldown
range, then releases both partial registries if validation fails. A zero-reference
owner can await collection by the next Move prepass. Eight mixed thirteen-recruit
continuations preserve4560 reference commits through12 seconds and9618 resumed
commits. The range-departure extension below uses those same stored deadlines;
the16-second second shared publication remains open. Version85 payloads
are rejected before restoring the world.


Captain range-departure reissue uses the existing Save86 actor reference and
creation-phase deadline across the shared point leg. The eight-save regression
covers11995/12000/12030ms around the exact12-second callback and reproduces7917
continuation commits. Inactive shared followers now receive the same finite
home/deadline and unique roster-index validation as private approaches. Six
malformed saves are rejected. Reissue transfers the final physical reference
without freeing a logically retired virtual actor between old-task stop and new
private-target admission. Full logical roster retention after physical completion
remains [the next movement task](retail-pathfinding-engine.md#captain-range-departure-into-a-private-approach).


Version87 adds each Move member's logical captain actor reference and registered
outer-circle membership. These survive physical task completion independently
of the private target reference, preserving inner/outer counts and the exact
second all-entered publication at16 seconds. All eight mixed13 checkpoints,
including15995/16000/16020/17000ms, reproduce11132 continuation commits across
the complete5462-commit journey. Completed logical members remain validated for
finite deadlines/home coordinates, unique indices and a live owned actor; malformed
logical references and booleans are rejected. Version86 payloads are rejected
before restoring the world. The bot script VM remains process-owned; this format
restores Move's membership and physical continuation.
See [logical-roster reentry](retail-pathfinding-engine.md#logical-captain-roster-survives-physical-completion).


Save87 also resumes largest-recruit public Stop before and after12+1 shared
admission: eight checkpoints per variant reproduce11764/12572 suffix commits.
Immediate post-Stop saves retain logical membership while physical references
are already detached; the surviving shared owner has one binding. Later saved
reentry reuses the shared pool slot with a new generation, and final natural
cleanup leaves neither generation live. Explicit cancellation of the last
binding remains separate from these largest-only controls. No format change
is needed. See [largest-recruit cancellation](retail-pathfinding-engine.md#largest-captain-recruit-stop-before-and-after-shared-admission).


Version88 persists the player AI VM initialization gate and accepts empty shared
physical Move groups awaiting their next owner visit. Public Stop detaches their
members immediately; shared publication precedes physical retirement, and the
following prepass collects the zero-reference parameter owner. Eight complete
all13 Stop checkpoints preserve pending, zero-reference and collected states.
A second StartCampaignAI must not replay main after loading. The creation gate
is persistent; private AI VM coroutines remain runtime-only and missing restored
continuations produce an explicit diagnostic. This does not restore the entire
AI VM. Out-of-range initialization bits and version87 saves are rejected.
See [final binding Stop](retail-pathfinding-engine.md#final-captain-binding-stop-and-repeated-ai-initialization).


Version89 integrates upstream interaction-route resumption. Its direction, goal
reference, goal origin/spawn, timestamp, radius, mask and wait diagnostics are
process-local caches and are cleared through `FIELD_RUNTIME` on save. Retail
fine/coarse buffers, velocity, native pose/clock, retry/delay and logical captain
membership remain serialized. The changed movement layout rejects version88
saves. `wc3_save.route_resume_cache_and_wait_diagnostics_clear_on_round_trip` covers the cache reset;
the retail captain saved-continuation regressions retain their exact suffixes.
See [upstream movement integration](pathfinding.md#upstream-ai-integration-and-retail-movement).


Version91 adds game-owned fine-cell insertion history. `WriteMoveSpatial` /
`ReadMoveSpatial` preserve ordering across overlapping target/blocker queries;
rebuilding from edict order would change identity termination after a save.
Records contain plain bounds/ranks and owning indexes, with no process pointers.
The loader rejects exhausted counters, invalid extents/ranks, duplicate or
inactive owners and truncated records. Ten malformed inputs and a valid private
serializer round-trip are covered by
`wc3_save.fine_spatial_history_rejects_invalid_records`. The public overlapping
Smart scenario matches eight saved continuations in both editions. Network
layouts are unchanged. See [pathfinding insertion history](retail-pathfinding-engine.md#overlapping-targets-retain-fine-cell-insertion-history).

Version93 stores independently published adaptive hierarchy state after terrain:
all four map dimensions and the exact four-lane class bytes. A terrain write
can leave the hierarchy stale until a regional footprint refresh; recomputing
it globally during load would change subsequent routing. Read validates the
complete shape/extent/classes before copying; restored entities then rebuild
fine masks and static producer snapshots without publishing pending edits.
Route revisions rebind to the loaded map lifetime rather than the legacy field
epoch. Eight saved public terrain-edit/Move continuations retain4164 original
commits and all observed regional classes. JSVM remains8; format92 and all older
layouts are rejected. See [regional publication](retail-pathfinding-engine.md#fine-terrain-edits-retain-regional-hierarchy-publication).


Pathfinding payoff85 adds the Move-owned region sample and validity bit in
Save94. This baseline is independent of predicted presentation `old_origin`:
a load immediately before region entry, callback teleport or callback removal
must preserve the same next transition. The public original journey checks
eight saved checkpoints and3418 subsequent motion commits, including10/25/50ms
server-frame batches. See [region callback lifecycle](retail-pathfinding-engine.md#region-callbacks-observe-committed-movement-and-retain-forced-changes).


## Suspended point orders and independent route lanes

Save95 retains `moveFineRoute_t.adaptive_mask` and `group_mask`, separately
from the current fine collision query. It also retains `movement.pause_order_id`,
`pause_resume_pending` and the primary `pause_deadline` time/epoch/span.
Loading during pathing-off travel, paused displacement or the pending resume
restores route ownership and delayed reactivation through the normal Move
scheduler. Eight checkpoints reproduce6171 original suffix commits, including
10/25/50ms frame batches. Older versions are rejected; no migration is supplied.
See [pathing/pause lifecycle](retail-pathfinding-engine.md#pathing-queries-and-scripted-pause-preserve-distinct-owners).

## Flight spatial history and primary-clock morph deadlines

Save96 preserves active flight fine rectangles and link ranks. These records
have category zero but remain spatially linked; older engine saves removed
them and cannot provide the same return-to-ground history. The field layout
is unchanged, but its meaning changes, so older versions are rejected.
Chaos deadlines and Move's retained type-rebind deadline use the saved primary
clock and now dispatch every primary quantum through the ability timer hook.
See [movement-mode policy](retail-pathfinding-engine.md#movement-modes-select-routing-policy-independently-of-spatial-membership).

Format97 retains the applying Slow/Bloodlust ability rawcode and source
incarnation in existing status fields. Public buff queries/removal and restored
Move caps use that owner after load. Nine original modifier boundaries reproduce
4356 saved suffix commits; old format96 is rejected rather than inventing missing
origin semantics. See [modifier evidence](retail-pathfinding-engine.md#temporary-speed-modifiers-publish-through-their-applying-owners).

## Way Gate edge ownership (Save98)

The sparse Way Gate record retains its native1..255 edge ID and allocation
attempt. Exhausted zero IDs survive load and do not retry on activation or
destination queries. Duplicate ownership and active/configured zero IDs are
rejected. See [the runtime contract](way-gates.md#native-allocation-lifetime).

## Way Gate source publication history (Save99)

The sparse ability record retains authored source half extents. The adaptive
payload appends the base marker plane after the four lanes' published hierarchy
classes. Class bytes must be0..2; source IDs use the entire byte domain.
Loading restores both arrays directly, preserving unconditional overlap erasure
rather than restamping surviving gates. Two actual removal/save/load sequences
retain complete native publication hashes and subsequent ordinary routes.
Older versions are rejected. See [overlap routing](retail-pathfinding-engine.md#way-gate-overlap-publishes-ordinary-routing-history).

Format107 replaces inline animation request/property arrays with shared immutable
records. Both pointer fields are runtime-only. The edict payload writes the
logical 80-byte request and 128-byte property buffers; load rejects unterminated
buffers, interns their text, and resolves the selected animation after rebinding
object data. This retains per-unit edits independently of later type defaults.
Model resource resets discard resolved selections while retaining logical text.

### Installed formation rank (Save110)

Save110 stores Move's installed low-nibble `formation_rank` as `F_INT`.
Construction/type rebind install it before ability initialization; a row-pointer
rebind or metadata edit alone must not change it. Group layout reads the
instance value. Writers/loaders reject ranks above15; version109 is rejected
rather than deriving a historical installed value from the current type row.
See [mixed-rank evidence](retail-pathfinding-engine.md#mixed-authored-ranks-install-before-formation-layout).

### Physical Alt point modifier (Save111)

Save111 updates the transient client-menu mapping for `order_alt`; it is
`F_IGNORE/FIELD_RUNTIME` and is explicitly cleared on restoration. The
accepted physical group's existing saved flags retain its formation policy.
No saved queued Alt reconstruction policy is invented: retail's original
request lifetime and later fresh requests are distinct. Save110 is rejected
before restoring the world. See [actual UI formation policy](retail-pathfinding-engine.md#ordinary-and-alt-formation-ticks-retain-the-actual-ui-policy).

### Retail Move and upstream ability lifecycle merge (version112)

Format112 combines the verified Move records with upstream’s mandatory owner/target
incarnation guards and updated optional ability pool layouts. Both parent formats
(111 and upstream65) are rejected. Timed status source pointers remain in the
owned sparse status pool; loading rebuilds its pointer references before resolving
source generations. Shared channel and missile constructors publish goal changes
through Move and retain the projectile producer’s scheduler/pose initialization.

### Counted scalar timers and callback clocks (Save113 / JASS9)

Save113 retains authored timeout separately from effective scalar period,
120-second segment counts/residual, frozen paused remainder, raw deadline and
registration serial. Timer publication/source clocks and the serial allocator
are persisted. Borrowed due-clock state is retained in JASS snapshot9 for saved
callback contexts; yielding releases it. Snapshot8 and Save112 are rejected.

The indexed scalar heap, per-timer heap index and active C millisecond timer
bitset are derived state. Load clears and rebuilds them from logical running/
paused records. The reader rejects nonfinite scalar timer inputs/deadlines,
impossible segment counts and running requests outside the adjacent epoch
domains before queue reconstruction. Five actual public getter-driven Move
continuations cross segmentation, pause/resume and the300-second wrap. See
[counted timer evidence](retail-pathfinding-engine.md#counted-timer-requests-use-their-own-scalar-clock).

### Callback mutation and deferred public timer release (Save114)

Save114 adds each scalar timer's resume phase and logical destroyed/pending-release
flags, plus the path owner's registration serial in the shared timer sequence.
The authored follow-up after a resumed callback and exact deadline ties therefore
survive restoration. JASS snapshot9 remains unchanged. Save113 is rejected.

Pending-release links and the list head are derived, like heap indexes; loading
reconstructs them from flags without resurrecting a destroyed public handle.
The existing scheduled-frame runtime flag is intentionally not serialized.
The regular frame owner restores that execution context before draining timers;
a direct test harness must do the same. Five public callback-mutation Move
continuations and the independent condition-pause/pending-retirement test cover
these observation boundaries. See
[callback mutation evidence](retail-pathfinding-engine.md#timer-callback-mutations-preserve-heap-order-and-deferred-release).

### Captain siege roster range snapshot (Save115)

Save115 adds`movement.captain_actor_siege`, the roster-derived retail captain6c.20
flag. It is refreshed at logical attach/detach, rather than reconstructed from
current weapons after loading. Physical approach ranges remain separately saved
in their Move member records. A malformed Boolean or a siege flag on a noncaptain
actor is rejected. Focused creation/weapon-edit/withdrawal round trips and the
prior114 format rejection test cover this change. See
[captain range evidence](retail-pathfinding-engine.md#captain-approach-ranges-retain-the-siege-roster-snapshot).
