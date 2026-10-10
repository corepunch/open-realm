# Warcraft III status effects

## Removal lifecycle

`unit_removestatus(unit, slot, reason)` dispatches `A_STATUS_REMOVE` while the
old slot is live, then clears it. Removal reasons distinguish expiry, dispel,
steal, replacement, death, source loss, transform, and script cancellation.
`unit_expirestatus()` remains a compatibility wrapper. Replace retires its
previous owner; Refresh and Stack retain their existing status instance.

## Source identity and application

`unit_applystatus(unit, &app)` accepts a `status_application_t` and returns the
live slot or `NULL` for invalid input or capacity exhaustion. The old insertion
functions remain wrappers. `source_ability` identifies the owning procedure
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

Spell Steal transfers only statuses explicitly marked positive and
transferable. The initial opted-in family is Inner Fire. The receiver slot is
allocated before removing the original, so capacity failure leaves the source
untouched. The transfer preserves level, remaining duration, source ability,
payload, state and numeric modifiers, while attributing the new application to
the stealing caster. Other statuses remain unsupported until their periodic,
TargetArt, callback, and source semantics can be safely reconstructed.

The serialized classification advances save format 81 to 82.

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
