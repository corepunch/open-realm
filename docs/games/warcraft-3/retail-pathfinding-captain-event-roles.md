# Captain roster and target event ownership

Payoff378 preserves the different subscriptions owned by roster membership and
retained targets. A roster member subscribes to death and owner changes. Selected,
follow and backup target roles additionally subscribe to target loss. When one
identity has several roles, each event has one callback and keeps its original
registration rank. Adding a target role appends its missing target-loss entry;
removing that role preserves the roster's death and owner entries.

Native roster attach `9cf680` passes enable/lost/death/owner values `1,0,1,1` to
`9d4a50`, and detach `9d5610` disables the same pair. Retained-target producers pass
`1,1,1,1`. The engine previously registered all three events for roster members.
Cargo loading therefore withdrew logical and physical Captain membership on
TargetLost, and RemoveUnit withdrew a member before deferred destruction. Both
failures reproduced on the prior pre-Ground module.

The proper role masks exposed another omission: the engine never published the
separate target-death registry. Native death commits life zero and its dead flag,
then publishes TargetLost `651010` and Death `650d80` before cancelling the old order
and physical task. The engine now preserves that frontier. Its later Move death
cleanup delivers generic `A_TARGET_REMOVED` notifications without publishing
TargetLost again. Ordinary Remove retains its original entity-count snapshot from
before its synchronous TargetLost callbacks. Same-incarnation checks protect the
death continuation against direct free/reuse; recursive death sees the committed
dead flag. Format 214 is required because the saved subscription meaning changes, even though no new fields are added. Format 213 is rejected before world restoration; no migration is provided.

The original three regressions passed 136/164 assertions per edition before the
mask change. A precise callback-frontier regression then passed 46/64 before the
early death producer was implemented. All setup and save guards passed. That test
checks the committed dead flag and zero life, original Move head, queued successor,
actual movement group and pre-retirement ability ownership at the Lost and Death
callbacks. A separate save regression first passed 6/10 assertions under format 213, proving the writer and early rejection contract were incorrect. The final eight-test gate passes 376 assertions in both Classic and TFT,
including the existing cargo-return and partial-removal tests. A separate older
fixture manually assigned its saved roster without registering its event owner;
two actual subscription refresh calls preserve all 85 of its original assertions.

Two clean read-only Frida observations of Cargo329assault and an observer-free
control agree on 678 public markers. All 1,264 semantic rows repeat after only one
raw Captain pointer alias (`0xc242264` versus `0xc222264`). Literal raw sequence
values 2..1265, complete row order, primary clock words, flags, call sites and
canonical unit/mover identities are retained and checked. Wisp roster registration
at 541 excludes target loss; loaded TargetLost 551 preserves membership; Captain
recenter 579 succeeds; death 620 withdraws it. Early failed captures and a recovered
output are diagnostic and are excluded from the accepted bundle.

The portable oracle contains 2,287 original instruction guards, including the
separate early death producer. Ghidra comments are saved with identical readback.
Seven integrity tests reject damaged sequence values, alias maps/records and
subscription masks. The unchanged 363, 368, 369 and 373 strict contracts pass after
this change; full save and order-lifecycle suites pass 323 tests/87,770 assertions
per edition. The precise public script death-family bridge, broader Town preferred
primary, transport selection, full Captain journeys and retail save files remain
within existing scope; these results do not certify their complete ordering.

The exact isolated checkpoint also exposed two fixture assumptions. Five absolute snapshot paths shared an 80-byte buffer and collided under its longer temporary directory. The fixture now uses full path storage and checks each formatted path fits, preserving every trace assertion. The fixed-step server fixture now explicitly enters the active game state before exercising the scheduler. Both Captain journeys and the clock pass 222342 assertions in three tests per edition. The failed format 213 checkpoint and its unchanged-source reports remain diagnostic evidence; the final format 214 checkpoint must use the exact staged tree.

A separate upstream merge correction preserves explicit status metadata alongside the existing ability-owned removal policy. The failing baseline passed 43/59 assertions in four tests: generic classified statuses were retained, known-owner undispellable statuses were removed, and script removals reported expiry. The correction preserves native owner category selectors, rejects explicit undispellable records, and accepts complete unambiguous generic metadata when the owner has no classification. Script removals use the shared inverse with the SCRIPT reason; natural expiry retains EXPIRE. The four regressions pass all 59 assertions in both editions, and 830 distinct affected tests pass 152412 assertions per edition. This is engine merge integration evidence, not a new native classification claim.
