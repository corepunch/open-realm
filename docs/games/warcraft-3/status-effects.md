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
