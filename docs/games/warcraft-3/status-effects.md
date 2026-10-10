# Warcraft III status effects

## Removal lifecycle

`unit_removestatus(unit, slot, reason)` dispatches `A_STATUS_REMOVE` while the
old slot is live, then clears it. Removal reasons distinguish expiry, dispel,
steal, replacement, death, source loss, transform, and script cancellation.
`unit_expirestatus()` remains a compatibility wrapper. Replace retires its
previous owner; Refresh and Stack retain their existing status instance.

## Source identity and application

`unit_applystatus(unit, &app)` accepts a `status_application_t` and returns the
live slot or `NULL` for invalid input or capacity exhaustion. The legacy
insertion functions return `false` for invalid input or a full bounded
container, so callers can propagate allocation failure. The generic spell
wrapper returns the exact slot produced by insertion rather than re-looking up
a rawcode after failure. Producers that spend mana, start cooldowns, or make
irreversible changes still need a preflight in validation and must handle a
failed final application. `source_ability` identifies the owning procedure
separately from `data`, which remains a legacy rawcode or ability payload.
Callbacks prefer `source_ability`, falling back to legacy `data` while older
ability families are migrated. Replacement sets the new identity and payload;
Refresh and Stack keep the existing owner and payload while changing lifetime
or count. Source pointers retain their spawn generation for safe validation.

Adding the serialized source identity changes the save layout; save format 79
rejects earlier versions.

## Composable categorical contributors

`status_application_t.state_mask` installs categorical contributors on an
individual status slot. `unit_hasstatusstate(unit, state)` accepts one state
bit and returns true if any live slot supplies it; expired slots are ignored
before cleanup. Removing or replacing a slot retires only its contribution.
Refresh and Stack retain the existing owner and state mask.

The state vocabulary includes STUNNED, ROOTED, SILENCED, SLEEPING,
MAGIC_IMMUNE, INVULNERABLE, and ETHEREAL. These are separate from ability
rawcodes and do not imply innate unit classifications or script-controlled
flags. Existing rawcode-derived stun remains supported; explicit STUNNED
contributors also update the `ent->stunned` cache on status refresh. Gameplay
consumers and Ethereal behavior are migrated in subsequent phases.

Adding serialized `state_mask` advances save format 79 to 80; prior formats
remain rejected.

## Ethereal and status invulnerability

`BHbn` contributes ETHEREAL. Ethereal attackers cannot make ordinary attacks;
physical attacks cannot damage Ethereal targets, while Magic attacks and spell
damage receive the Warcraft 66% bonus. Banish slows movement by 50%. Queries
ignore expired statuses, restoring normal eligibility as the slot expires.

`BHds` contributes INVULNERABLE, and the damage gate checks status contributors
alongside the historical entity boolean. Divine Shield retains its existing
thinker and boolean restoration for save and script compatibility. Integer
damage rounding and cross-family parity should be checked against retail data.

## Status-owned numeric modifiers

Each live status slot may own up to four typed numeric contributions. The
resolver adds independent values and selects the strongest value once for each
nonzero family and modifier type. Strongest policies require a family ID;
family zero is reserved for independent additions. Ability code still decides
which gameplay values and stacking families apply.

Removal and expiry stop contributions with their owner. Refresh retains its
existing descriptors; Replace clears the old slot before initializing the new
one. Phase 6 introduces the resolver without wiring it into existing combat or
movement arithmetic, avoiding double application during the migration.

The serialized modifier descriptors advance save format 80 to 81.

## Owned authored modifier migration

Cripple, Bloodlust, Slow Poison, Inner Fire, and Faerie Fire populate the
status-owned descriptors. Movement, attack speed, attack damage, and armor
consumers use the shared resolver for those contributions and remove their
previous ability-specific adds to prevent double application. Contributions
are combined before the shared speed multiplier; other ability bonuses retain
their existing consumers. Retail stacking and rounding still need parity
verification.

## Shared periodic deadline consumption

Poison Attack, Disease Cloud, and Entangling Roots consume one-second pulses
through `unit_status_take_due_tick()`. It advances the deadline before ability
damage, preventing recursive status updates from repeating the same pulse.
It rejects unscheduled and expired deadlines, while retaining catch-up timing
after skipped simulation frames. Ability code continues to own source checks,
damage values, and removal semantics. Existing `next_tick` serialization is
unchanged; tests cover due, catch-up, recursion, expiry, and invalid deadlines.

## Dispel and transfer policy

`buff_flags` explicitly classifies positive/negative, magical/physical,
undispellable, and transferable status semantics. Zero flags preserve the
existing timed-status dispel rules, including Timed Life and authored Cyclone
exceptions. Purge and Dispel Magic share `unit_status_can_dispel()`.

Spell Steal transfers only statuses explicitly marked positive or negative
and transferable, with direction checked against the source unit's relation
to the caster. Supported families are Inner Fire, Bloodlust, and Faerie Fire;
nearby allies receive positive effects and nearby enemies receive negative
effects. The receiver slot is allocated before removing the original, so
capacity failure leaves the source untouched. The transfer preserves level,
remaining duration, source ability, payload, state and numeric modifiers,
while attributing the new application to the stealing caster. Slow, Frost
Armor, Unholy Frenzy, Rejuvenation, and Anti-Magic Shell remain unsupported:
their periodic callbacks, healing/drain schedule, vision or absorption state,
and TargetArt ownership are not all reconstructible from a status snapshot.

The serialized classification advances save format 81 to 82.

Modifier queries now combine strongest families in bounded stack storage while
visiting statuses once. Categorical-state lookup remains a bounded linear scan
of the fixed status array.

## Persistent status TargetArt

`WC3_STATUS_BUFF_TARGET_ART` opts a status into lifecycle-owned TargetArt.
`unit_status_enabletargetart()` marks an applied slot and creates the effect;
status application reuses the existing buff-target deduplication helper, and
removal destroys it after the inverse callback, regardless of removal reason.
The flag reuses serialized `buff_flags`, so this addition does not change the
save layout.

Entangling Roots uses this shared owner instead of managing its TargetArt in
ability callbacks. Other effects remain independently owned until explicitly
migrated. Persistent effects are deduplicated by buff and target incarnation;
per-source multiplicity and looping sounds need separate lifecycle handling.

## Bounded status capacity preflight

`unit_status_checkapplication(unit, &app)` classifies without mutation:
`INVALID`, `REUSE`, `FREE_SLOT`, or `FULL`. Producers that cannot safely spend
mana or resources without applying a status should check before committing
effects, and still check `unit_applystatus()` because capacity can change.
Same-rawcode applications reuse their slot even when the array is full; a ninth
distinct rawcode returns `NULL` without evicting an existing status. The legacy
void wrappers remain available, while explicit callers can handle failure.

The bounded status array now carries a per-unit generation identity and an
explicit `DEFAULT` or `INDEPENDENT` policy. Independent applications reuse only
the same source incarnation; another source receives its own duration and tick
record. `unit_findstatus()` remains the deterministic first-by-rawcode lookup;
`unit_findstatussource()` addresses a specific source. Instance IDs and policy
are serialized in save format 83. No ability family is opted into independent
stacking yet: current reviewed poison/status handlers rely on rawcode-wide
refresh/removal semantics, so enabling one without changing its callbacks
would be unsafe. HUD and TargetArt remain rawcode-deduplicated presentations.

## Status integration safeguards

Generic application only refreshes authored numeric modifiers for the status
that was applied. Unknown/custom status modifiers remain intact. Default
applications do not reuse independently owned slots; independent sources
retain their instance and timer even when a legacy same-rawcode buff is added.
Spell Steal passes a terminated buff ID and refuses to replace a recipient's
existing same-code status. Its existing limited transfer-family allowlist remains.

## Integration hardening after patches 12–15

All `unit_removestatus()` callers now receive immediate derived-state and HUD
refresh after the removal callback. Abolish Magic applies the shared dispel
eligibility predicate, preserving protected timed statuses. Source-specific
lookup ignores expired records awaiting cleanup. Spell Steal considers other
nearby eligible recipients if an earlier candidate has no free status slot or
already carries the buff; transfer still remains opt-in for reviewed families.
Rejuvenation validates status capacity against the caster's ability level.
Delayed and multi-target status applications remain impact-time decisions;
resource/cooldown refund policies require per-ability evaluation rather than
a blanket rollback. Independent stacking remains opt-in only.

## Follow-up integration hardening

Avatar expiry called by status removal performs its stat inverse without recursively
removing the same status. Explicit Avatar disable still removes its buff.
Attack profile selection prioritizes an eligible Magic attack against Ethereal
targets, matching the target gate for dual-weapon units. Expired statuses no
longer receive A_STATUS_REFRESH callbacks. Cast preflight treats INVALID as a
failure as well as FULL. These changes have source-level validation only.

## Removal batching and callback guard

`unit_removestatus_deferred()` performs the same status callback and visual
cleanup as ordinary removal, without reconciling derived state after each slot.
The expiry loop and multi-buff dispels reconcile once after the group, while
replacement reconciles after the new record is installed. A per-call removal
stack rejects attempts to recursively remove a slot already being retired.
Status authored modifiers are installed only when the slot has no preexisting
descriptors, preventing refresh from erasing externally owned contributions.

This work does not enable independent stacking on normal ability families or
make delayed multi-target spell outcomes transactional. Those paths still need
ability-by-ability review.

## Status application follow-up

Targeted authored statuses now pass the actual applying ability code to
`unit_applystatus()` before installing stock modifier descriptors; this avoids
prematurely populating a stock-alias modifier before custom Data is known.
Generic `ROOTED` contributors block translation and new Move orders, while
Roots-specific disarm behavior stays separate. An exhausted Anti-Magic Shell
uses the normal status-removal lifecycle. Incinerate's death-triggered mark
consumption is intentionally separate to avoid duplicate explosions.

Remaining work includes ownership-aware replacement of modifiers already held
on retained stock statuses, ability-specific capacity checks for delayed and
area spells, and expanded safe Spell Steal reconstruction. No retail parity
verification was performed for these changes.


## Authored numeric modifier ownership (save v84)

Each modifier descriptor has a serialized `owner`: zero means an external/custom
provider and `WC3_STATUS_MOD_OWNER_AUTHORED` identifies a stock AbilityData
contribution. Applying a stock buff refreshes its authored contributions by
(type, owner), without overwriting or clearing external descriptors in the
same status. New authored descriptors append into available slots. When all
four modifier positions belong to other providers, authored installation
fails closed rather than evicting a custom modifier; the general capacity
limit still requires separate design if more than four contributors per buff
are needed. This is intentionally separate from spell-cast capacity preflight.

Save format 84 rejects prior saves because modifier descriptor layout changed.

## Targeted capacity follow-up

Anti-Magic Shell (`Aams`, `Aam2` and corresponding creep/item handlers)
checks the target's actual ROC/TFT buff identity and status capacity during
`A_VALIDATE`, before the shared cast pipeline commits resources. `REUSE` is
allowed; `FULL` and `INVALID` are rejected. At execution the application can
still fail if the target changes between validation and impact.

Possession Two additionally checks both target and caster status capacity
before creating its channel thinker; this is an impact-time defensive check,
not a complete two-target transactional allocation. Area Roar continues to
allow partial recipient success rather than rejecting the entire cast when
some nearby units are full.

## Spell Steal eligibility hardening

Spell Steal now validates a transferable source and a receiver with a free
status slot before the cast commits. The same selection policy runs at impact:
it searches later eligible source statuses when earlier ones have no receiver.
A post-validation change can still cause a no-op, as with other delayed spells.
Transfer remains restricted to the reviewed simple status families; arbitrary
ability callback state and visual reconstruction are not newly supported.

## Incinerate death consumption

Incinerate records the originating ability explicitly on its hit mark. Its
`A_STATUS_DEATH` callback consumes the mark through deferred lifecycle removal
before immediate explosion damage, preventing nested death dispatch from
spawning another blast. Invalidated sources also retire the mark through the
shared lifecycle without creating an explosion. The dying unit is not refreshed
during death dispatch; ordinary expiry and dispel continue to use their normal
removal paths.

## Explicit magical dispel eligibility

Dispel Magic, Purge and Abolish Magic use `unit_status_can_magic_dispel()`.
It preserves historical handling for unclassified legacy timed statuses,
removes explicitly magical statuses when otherwise eligible, and skips
explicitly physical-only or ambiguously dual-classified statuses. Explicit
undispellability and authored special cases still take precedence. The more
general `unit_status_can_dispel()` continues to define baseline status
eligibility for non-magical lifecycle consumers. New status producers must set
the correct classification to opt into the stronger distinction.

## Classified existing spell families

The following application sites now tag their known stock buff identities as
magical without adding transfer eligibility: Rejuvenation (`Brej`) and Roar
(`Broa`) are positive; Curse (`Bcrs`), Unholy Frenzy (`BUhf`/`Buhf`),
Cripple (`Bcri`) and Soul Burn (`BNso`) are negative. These tags are applied
only when the returned slot has the expected buff identity; a custom BuffID is
left unclassified. Existing transferable flags on Bloodlust, Faerie Fire and
Inner Fire are unchanged. Physical-only families are listed below.

## Independent Disease Cloud infections

Disease Cloud (`Aapl`, including alias `Aap1`) now uses independent `Bapl`
status instances keyed by source incarnation. Each emitter owns its own duration
and one-second damage schedule; re-exposure to the same source refreshes
only that instance without resetting its pulse deadline. Dispel or expiry
of one instance does not remove another source's infection. Allocation
failure leaves existing infections intact; the generic HUD remains
rawcode-deduplicated. Other poison families remain legacy/rawcode-owned.

## Physical-only Warcraft ability statuses

Stock Ensnare/Web binds (`Bens`, `Bena`, `Beng`, `Bwea`, `Bweb`), poison
attack tokens (`Bpoi`, `Bpsd`, `BIpb`, `BIpd`) and Disease Cloud infection
(`Bapl`) are classified as negative physical statuses at their owning
application sites. Magical dispel abilities leave them in place, while generic
status removal remains available for expiry, scripted removal and death.
Custom BuffIDs remain unclassified so stock semantics are not imposed on map
content. These physical classifications do not make the effects eligible for
Spell Steal.

## Possession Two status allocation transaction

`Aps2` validates that both channel status records have free slots before
committing resources. It does not replace existing `Bpos`/`Bpoc` ownership.
Execution rechecks capacity, applies target then caster status, and removes
the newly installed target status if the second application fails. The channel
thinker and target invulnerability are created only after both succeed. If
capacity changes between validation and execution, the cast may already have
committed its cost before execution detects the failure.

## Lightning Shield capacity safeguard

Lightning Shield validates the authored target buff before committing a cast.
Its execution path rechecks by applying the status first; if insertion fails,
it does not create or refresh a damage thinker. Existing instances may be
reused according to the normal status stacking policy. Area buffs such as Roar
retain per-target partial-success semantics, and Purge may dispel even if its
subsequent slow status cannot be allocated; these have separate ability-level
failure semantics.

## Area status partial-success regression coverage

Roar remains a best-effort area cast: when one friendly target has no status
capacity, other eligible friendly targets still receive the authored buff and
damage bonus. Purge dispels eligible effects before installing its slow; a full
array of undispellable statuses remains intact, while removing a dispellable
status may free a slot for the slow. Tests exercise the production spell
handlers with authored fixture data rather than introducing global capacity
rollback or all-or-nothing area semantics. They do not cover every area spell
or projectile impact-time capacity change.

## Delayed-status producer allocation safeguards

Aerial Shackles preflights the victim's authored target buff before cast commit,
then requires successful insertion before activating its channel thinker or
first damage tick. It reserves the thinker first and frees it if status
insertion fails. Item Defense AoE counts only recipients whose timed status
was successfully applied; an entirely full area does not report successful
status application. Other multi-target spells retain their individually
documented best-effort behavior.

## Channelled status instance ownership

Aerial Shackles captures the applied target status `instance_id` in its
channel thinker. Periodic damage and cancellation cleanup verify that exact
instance rather than relying on the buff rawcode alone. Dispel or replacement
ends the old thinker without damaging the target or clearing its replacement
buff. Shackles remains a single rawcode-owned ability family; independent
simultaneous sources are not enabled.
