# Sleep suspension and the initial damage guard (Group367)

This investigation belongs to the existing **GROUP-03.2** acquisition and concrete
status-owner audit. The engine regressions were established before the port;
this document does not claim implementation completion.

Two original retail observations and an uninstrumented control produce the same
45 public markers on `Sleep367.w3m` (SHA-256
`f3f76c97264796a235c1e5b71e935117b095c066091965858b1c30829c8b535f`).
The public probe casts stock `AUsl` on an already moving Footman, pauses the
caster, issues another Move during Sleep, damages during and after the initial
guard, and explicitly removes the buffs in a separate scene.

- `BUsl` pushes the private suspension order **851973**. Public Move returns true
  while this suspension remains the current order; the accepted Move runs after
  the Sleep owner ends.
- The initial `BUsp` lasts authored `DataA` (stock rank one: two seconds).
  Incoming damage during that interval leaves life unchanged and Sleep present.
  After `BUsp` expires, damage applies and requests Sleep termination. The public
  query immediately after damage still sees `BUsl`; a later boundary releases it.
- Both Unit execution/order ability-disable aggregates remain zero. Scripted
  `PauseUnit` therefore cannot stand in for this internal suspension owner.
- Pausing the caster does not terminate either victim owner. Explicit buff removal
  releases the victim's suspension and the retained Move subsequently executes.

`AbilitySleep_ApplySleepAndInitialPause` at `6f50c100` refreshes existing `BUst`
if found; otherwise it creates or refreshes `BUsp`. This producer does not create
a new `BUst`. Main Sleep initializer `6f518290` pushes suspension with argument
zero and removal `6f50da20` pops it. SleepPause initializer `6f518320` acquires
damage prevention and its `6f500430` inverse releases that contribution.

The separate stock public attachment probe has two observations and a control
with 51 identical markers. `UnitAddAbility` rejects `BSTN`, `BUst`, `BUsp`, `BUsl`,
`BPSE` and an unknown rawcode, while the positive control `AUsl` attaches at rank
one. This establishes only these stock-data attachment results. Factory
registration and the absence of direct callers do not prove public
unreachability of the concrete classes.

Ghidra progress is saved: `Group367Evidence.java` and
`Group367SuspensionEvidence.java` and `Group367ClockEvidence.java` export 2,246 unique instructions across 63
complete functions; the saved readbacks match exactly. The source map is
updated in `tools/ghidra/MapPathfinding.java`.

Reproduce the public scene with `group341_make_map.py`, the preserved
`Group338Hard.w3m` base, and `group367_sleep_probe.j`; observe it using
`group367_sleep_observer.js` through the owned research environment wrapper.
Completed captures and original exports are archived under
`GROUP-03.2/payoff367`. The reserved-identifier compile failure and the first
incorrect preload-filename run remain diagnostics and are excluded from the
accepted observations.

The new production-path regression covers both AbilityData schemas, stock and
non-stock initial durations, and warm/cold saved retained Move state. Its
unchanged-engine baseline must fail before any implementation change.

The first unchanged-engine run fails72 behavior assertions per edition
(88/160 passing), including the missing initial owner, unblocked early damage,
missing suspension head and immediate damage wake. Setup, public Move admission
and save/load succeed. The subsequent fixture revision explicitly advances the
primary clock while exercising the delayed release; its unchanged-engine rerun
also fails72 behavior assertions per edition (88/160 passing). These unchanged-engine results precede the Sleep implementation.

Twenty-six evidence-integrity regressions pass. The frozen contract contains 15
captures across 26 scenes, 332 public markers and 622 projected native/marker
rows. The three production regressions pass 640 assertions per edition in an isolated
Save211 production/test build from the reviewed index. The broader affected
lifecycle, unit, pause, spell, save and pool suites pass 1,033 tests / 145,233
assertions per edition.

The nested `Sleep367Nested.w3m` experiment contributes two observations and an
uninstrumented control with 82 identical public markers and 186 matching native
rows. Main Sleep and its initial guard each acquire an independent suspension
contribution. Scripted Pause adds a third contribution and its own order-disable
counts. Unpause releases those scripted counts while both Sleep contributions
remain; removing Sleep while scripted Pause remains does not release that owner.
Scripted Pause also stops the initial guard clock: damage after its original
wall-clock deadline is still blocked.

The initial-guard suspension argument is a witnessed caster pointer, rather than
a flag word. Its projection checks the raw argument against its witnessed
address and preserves the literal Unit slot and generation. Only that verified
heap address is replaced by the canonical identity; unknown or mismatched
associations are rejected. The initializer durations remain literal float words:
20 seconds for main Sleep, two seconds for the initial guard, and zero spell
duration adjustment in these scenes.

The separate nested-Pause production regression fails 96 behavior assertions
per edition against the unchanged Save206 engine (280/376 passing). Both
AbilityData schemas, both attachment orders, removal under scripted Pause and
warm/cold states are covered. Scripted Pause setup, admission rejection and
save/load pass; the missing initial guard and its retained clock fail before
any Sleep implementation change.

A separate authored-alias experiment compares stock Thunder Bolt and Sleep with
six custom spell clones carrying `BSTN`/`BUst` and other buff-ID substitutions.
Two observations repeat all 82 native/marker rows, and their 58 public markers
also match an uninstrumented control. The concrete owners remain `CBuffPause`
(vtable RVA `adcec8`), `CBuffSleep` (`b0aa60`) and `CBuffSleepPause` (`b0a6e4`).
No Stun or SleepStun factory call occurs in these eight scenes. This rules out
these particular spell-owned alias substitutions as producers; it does not
establish global or custom-data unreachability.

The clock follow-up saves and reads back 34 complete functions, including the
shared counter callbacks and embedded timer. Main Sleep and its initial guard
both freeze their remaining expiration time on the first clock-pause
contribution and resume it on the last inverse. Script Pause acquires this clock
contribution before ability availability, and releases availability before
resuming clocks. The embedded request uses the primary clock; a direct zero
reschedule clamps to the original scalar word `0x38d1b717` (about 0.0001 seconds).
Nonpositive authored initialization follows a separate permanent path.

The engine implementation under Save211 owns independent internal suspension
contributions, per-owner primary requests, saved frozen remaining times, the
initial invulnerability inverse and deferred damage wake. Three focused tests
pass 640 assertions in each edition. The earlier full affected suite passed
1,028 tests / 130,795 assertions per edition before the final refresh change;
that result is preserved separately from the focused refresh gate.

`Group367Refresh.w3m` adds two complete observations and an uninstrumented
control with 96 identical public markers and 232 identical projected records.
It proves that a positive recast preserves the existing main owner and rearms
its primary request while Script Pause still holds its clock counter. The main
owner expires while the unit remains paused; the separately frozen initial
guard expires only after Unpause, and the held Move then resumes. The failing
engine baseline passes 92/96 assertions per edition, with four owner-identity
failures; the implementation passes all 96 afterward.

The actual native initializer durations are three seconds for the main owner
and two seconds for its initial guard. This map's attempted half-second `Usl1`
modifier has a zero data pointer and is ignored; it supplies no half-second
runtime claim. The accepted canonical observer derives Unit identity directly
from the literal slot/generation pair. Earlier heap-identity observations with
two inconsistent derived identities remain rejected diagnostics; they are not
silently normalized into the accepted repeated stream.

All progress remains within the existing GROUP-03.2 audit. Concrete Stun and
SleepStun public producer coverage remains unfinished.
