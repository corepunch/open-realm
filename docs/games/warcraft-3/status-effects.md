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
