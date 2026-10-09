# Warcraft III Hide / Shadow Meld

## Scope

OpenRealm models Night Elf Shadow Meld as conditional gameplay invisibility, separate from both the explicit Hide order and the overloaded `RF_HIDDEN` presentation flag.

- `Ashm` maps to `CAbilityShadowMeld`. It owns passive/update state **and** exposes the stock `ambush` Hide order (`852131`).
- `Ahid` maps separately to `CAbilityShadowMeldAkama`. Warcraft ability metadata identifies it as the Akama Shadow Meld class; it also exposes `ambush`, but additionally suppresses automatic hostile acquisition without requiring an explicit Hide order.
- `AOwk` remains the independent Wind Walk implementation; Shadow Meld no longer receives `BOwk` or Wind Walk bonus-damage behavior.

## State and lifecycle

`edict_t.shadowmeld` stores the uninterrupted fade start, fade/active state, and whether the explicit Hide order is active. Scalar state rides in the raw edict save record; load rejects incompatible edict layouts using the saved `sizeof(edict_t)` header field.

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

The Akama class is now kept separate and its documented no-auto-acquire distinction is modeled. Other class-specific `Ahid` differences are not inferred without stronger evidence.

Owner/shared-vision Shadow Meld presentation now reuses the WC3 per-client vertex-tint datagram. During the authored gameplay fade, those viewers receive a presentation-only opacity that eases from the unit's authored alpha toward 35% of that authored alpha; active Shadow Meld remains at that 35% multiplier. The current curve is the standard smoothstep `t*t*(3-2*t)` approximation. Retail's exact final alpha and interpolation curve have not been recovered, so both values are deliberately documented presentation constants rather than compatibility claims. Hostile detector viewers receive the ordinary authored tint instead of the friendly ghost alpha. This presentation does not alter gameplay visibility, detection, selection, or authoritative `vertex_color`.

## Regression coverage

`games/warcraft-3/game/tests/t_shadowmeld.c` covers:

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
