# Warcraft III Hide / Shadow Meld

## Scope

**Retail checkpoint338 corrects the model below. The C implementation has not
yet been corrected.** Ability display names are not proof of class ownership.

- `Ahid` is retail `CAbilityHide`: an explicit hold-fire button, not a passive
  invisibility producer. Its class getter is `5170f0`, table `b0846c`.
- `Ashm` is retail `CAbilityShadowMeld`: it owns the invisibility contribution
  and fade/reacquisition timers. Its class getter is `5172a0`, table `b08110`.
- Stock Akama (`Naka`) has `AInv,Apiv,Ahid`, without `Ashm`. His passive
  invisibility comes from `Apiv`. The stock `Ahid` row's “ShadowMeld (Akama)”
  comment and fade-shaped fields do not establish a fade owner.

Addresses here are RVAs in the 1.27.1.7085 DLL, SHA256
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`.

### Original control and lifetime

`50ab40` sets the Hide instance's `100` flag once, acquires active ownership,
and increments the current Attack object's `220` suppression word through
`495740`. It then creates tasks `d0148`, `d014e`, `d0177` (Hold), `d0144`
(its button order), and action0. It does not call the invisibility acquire
service `6696e0`. `51d1e0` handles `d0178` with `50d940`: clear the instance's
`100` flag and decrement the **current** Attack object through `495750` if one
exists. This inverse does not retain an Attack-generation token.

`Ashm` uses `50abd0` for its explicit button, with its own `400` flag. Its
invisibility acquire/release services are `5067b0` and `509170`. They use the
unit's shared invisibility contribution and private primary-clock requests;
they are not the Hide button's ownership flag.

The [static proof](../../../tools/ghidra/fixtures/research/shadow338-original.json)
pins18 complete bodies,30 virtual-table words and96 direct calls to that DLL.
Its checker passes eight rejection controls and stops each table at `+344`;
it makes no implicit companion-ability construction claim. Run
`python3 tools/ghidra/research/shadow338_check.py --binary /path/to/game.dll --check`.

Two instrumented retail runs and a fresh unhooked control agree on all832
public records. Each instrumented run preserves1,107 native records. The four
scenes are Ahid-only at noon, Ahid-only at midnight, Ashm-only at midnight,
and both at midnight. No scene has Apiv. Each issues ambush at age20,
removes Ahid at30, and issues Move at35.

- Ahid-only never acquires the shared invisibility contribution, before or
  after ambush. Ambush succeeds at both times of day and sets Attack220 to1.
- Removing Ahid leaves that suppression at1; the later Move also leaves1.
  No inverse is observed at either operation. The detached ability is not
  dereferenced, so its later private flag value is unknown.
- Ashm-only and both-class scenes acquire the shared invisibility contribution.
  Their ambush invokes the Ashm button; Move releases its Attack suppression.
- No scene emits a public unit-spell phase. All viewer-query results remain
  literal in the reference; they are not equated with the private contribution
  count or used to infer the full detection policy.

Reproduce the accepted recording checks with
`python3 tools/ghidra/research/hide338_capture_check.py --check`.
The [portable reference](../../../tools/ghidra/fixtures/research/hide338-public-original.json)
includes the stock attachment rows, complete recordings and readable capture
sources. Ten rejection controls pass. This is original-engine evidence,
**not C parity**: the unchanged C baseline emits834 rows against832, with612
positional differences. Its first difference is scene0 tick0 time-of-day
(`0.100` versus retail `12.000`); preserve that failure independently of the
control/ownership corrections.

### Current C behavior and required corrections

The following describes the existing C code, not accepted retail behavior.
OpenRealm currently models both classes as conditional gameplay invisibility,
separate from the overloaded `RF_HIDDEN` presentation flag.

- `Ashm` maps to `CAbilityShadowMeld`. It owns passive/update state **and** exposes the stock `ambush` Hide order (`852131`).
- `Ahid` incorrectly maps to `CAbilityShadowMeldAkama`, shares the private fade,
  and suppresses automatic acquisition before an explicit Hide order.
- `AOwk` remains the independent Wind Walk implementation; Shadow Meld no longer receives `BOwk` or Wind Walk bonus-damage behavior.

## State and lifecycle

The `shadowMeld_t` component (`G_GetShadowMeld`, attached by the passive update or the Hide order) stores the uninterrupted fade start, fade/active state, and whether the explicit Hide order is active. Scalar state persists as a component payload in the typed pool section (see [pooled entity state](pooled-entity-state.md#optional-components)); load rejects older format versions.

Both `Ashm` and `Ahid` participate in passive Shadow Meld eligibility: effective night plus an idle/stationary unit. The passive fade duration comes from each effect's authored `Shm1` (`DataA`) value in seconds, including variant and custom rows; an absent or invalid value prevents activation and emits a bounded diagnostic. Losing eligibility resets the fade; daylight removes active Shadow Meld. Effective night comes from `G_IsNight()`, so normal and scripted/false time share the same authoritative clock.

Player-wide ability availability is checked per class for `ambush` validation/execution and class-specific acquisition/retaliation policy. If a unit has both classes, disabling one does not make that class inherit the other class's command or policy; the remaining available class can still provide passive Shadow Meld.

The `ambush` command is registered on both effect classes. Issuing it stops the unit and sets `hide_order_active`; while that explicit Hide state is set, `A_NO_ACQUIRE` suppresses voluntary hostile acquisition and `A_NO_RETALIATE` suppresses automatic counter-attacks after damage. Damage can still land during the fade, but it no longer makes the unit attack and restart its Hide fade. `Ahid` additionally owns the Akama-class no-auto-acquire rule even before `ambush` is issued. Explicit attacks and ordinary passive acquisition use the same `order_attack()` transition, so no attack can retain stale Shadow Meld state. Accepted `Stop` and `Hold Position` orders pass through the shared `A_ORDER_ACCEPTED` lifecycle hook: Stop retires explicit Hide but, because the unit remains stationary, passive Shadow Meld may begin a fresh fade; Hold Position likewise permits passive Shadow Meld but does not gain stock `Ashm`'s explicit-Hide hold-fire policy. Movement/order lifecycle messages and spell commits also break Shadow Meld; the `ambush` command itself is the spell-commit exception.

## Visibility

Shadow Meld does not set authoritative `RF_HIDDEN`. Instead, `S_UnitIsInvisibleToPlayer()` recognizes `S_ShadowMeldActive()` alongside the existing invisibility sources. This keeps visibility player-relative:

- owner/shared-vision players retain access;
- hostile players do not receive/target/acquire the unit normally;
- detector coverage reveals the unit only to eligible viewers;
- cargo, mine workers, training/revival placeholders, and other non-invisibility `RF_HIDDEN` users remain unrelated.

This also avoids one invisibility source accidentally clearing another source's `RF_HIDDEN` bit. Aura membership treats active Shadow Meld like the other gameplay invisibility sources: a Shadowmelded source does not provide an aura and a Shadowmelded recipient does not receive one. Detection remains a viewer concern and does not reactivate aura participation.

## Known gaps

OpenRealm's current classic Warcraft data baseline is 1.29.2. Patch 1.31 later made the Cloak of Shadows Hide variant usable during daytime and able to work with units that already have Shadow Meld. That exception should be driven by item/dataset provenance rather than making every Shadow Meld class daytime-capable.

Required changes: remove Ahid's autonomous fade and unconditional acquisition
policy; give its explicit button the original Hold task/control ownership
without synthetic spell phases; account for the raw Attack220 balance,
including suppression retained after detach, in save validation; and port
Ashm's actual shared contribution and primary-clock lifecycle. Do not borrow
the Binv/BOwk generation-bound inverse or forced-recovery wrapper for Hide.
The existing tests asserting passive Ahid fade or unconditional acquisition
suppression encode the old C model and require original-backed correction.

Owner/shared-vision Shadow Meld presentation now reuses the WC3 per-client vertex-tint datagram. During the authored gameplay fade, those viewers receive a presentation-only opacity that eases from the unit's authored alpha toward 35% of that authored alpha; active Shadow Meld remains at that 35% multiplier. The current curve is the standard smoothstep `t*t*(3-2*t)` approximation. Retail's exact final alpha and interpolation curve have not been recovered, so both values are deliberately documented presentation constants rather than compatibility claims. Hostile detector viewers receive the ordinary authored tint instead of the friendly ghost alpha. This presentation does not alter gameplay visibility, detection, selection, or authoritative `vertex_color`.

## Regression coverage

`games/warcraft-3/game/tests/t_shadowmeld.c` covers:

- per-tick update-dispatch gating correctly following immediate runtime ability addition/removal, authored list changes, and hero rank acquisition without a persistent ability cache;
- `Ashm`/`Ahid` no longer resolving to Wind Walk and remaining distinct effect classes;
- both classes exposing the stock `ambush` order id;
- class-scoped ability availability with both classes present, including the still-available class retaining passive Shadow Meld;
- Akama `Ahid` passive no-auto-acquire behavior;
- passive 1.5-second night fade;
- owner versus hostile invisibility query;
- owner/shared-vision presentation alpha using the documented smoothstep approximation without mutating authoritative vertex colour;
- daylight cancellation/reveal;
- Hide suppressing automatic acquisition;
- Stop retiring explicit Hide while allowing a new passive fade;
- Hold Position allowing passive Shadow Meld without Hide's hold-fire policy;
- movement lifecycle breaking Hide/Shadow Meld;
- any attack order immediately breaking Shadow Meld/Hide;
- a hidden unit responding to incoming attacks through the normal retaliation path;
- daytime rejection of the classic `ambush` order;
- shared aura coverage additionally verifies Shadowmelded aura sources and recipients are excluded.

See also [Invisibility mechanics](invisibility.md) for the shared detector/player-relative visibility contract and Wind Walk/Ghost/item invisibility ownership.
