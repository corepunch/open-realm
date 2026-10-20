# Moving-blocker yielding and identity lifetime

## Producer and decision contract

ROUTE-05.1 is integrated in Payoff162. `MoveRequest_SetFormationOptions`
(`89caf0`) sets canonical flags2/4/8 through the same Boolean. Flag8's sole
setter is `16dc70`; `16bdb0` copies the canonical request flags to the physical
group. Network selection dispatch derives the Boolean from packet flag0x10
(the Alt modifier). Ordinary public JASS point orders keep it clear. The engine
already stored the selected policy and saved physical group flags, but its
moving-blocker adapter omitted those flags when calling the scalar kernel.

Original `168360`, assembly16843f..16844e, compares resolved physical group
identity and its flag8. With the flag clear, a same-group encounter assigns the
requester a minimum four-visit wait, even when the candidate is slower. With
flag8 set, that shortcut is skipped: a slower same-player candidate instead
receives a minimum twenty-visit wait. Stationary, groupless or already-blocked
candidates are skipped. Different classes and faster/equal candidates retain the
requester-four policy. Comparison uses committed velocity squared with retail
scalar arithmetic, independently of authored speed.

Move now passes the actual retained physical group flags to the existing
`wc3_yield_decide` kernel. It uses the existing derived group binding; reads do
not allocate or introduce a new per-frame scan. No rawcode-specific policy or
new simulation flag is added.

## Removal, reuse and saving

`168070` copies the blocker registry `(slot,generation)` and raises the delay
with unsigned max. Public RemoveUnit does not change a waiter's delay or clear
its stored native identity. Registry reclamation occurs through a later clock
request. The stale identity fails generation resolution after slot reuse; the
next resolver entry clears it. A replacement reusing the public unit handle or
mover address therefore does not inherit the relationship.

The engine retains the equivalent resolved relationship through an edict
reference, clearing it at actual reclamation while preserving its countdown.
Its allocator retains the Quake-style reuse cooldown. The engine regression
uses actual public RemoveUnit, consumes four/twenty owner visits, saves and
restores the same continuation, then permits ordinary allocation to reuse the
freed slot. The replacement has a new incarnation and does not inherit a wait
link. This does not claim identical private registry layouts or allocation times.

The selected producer regression uses actual `G_ClientCommand` ordinary/Alt
orders, then literal captured committed fine velocities scaled through the
retail scalar operation. Both choices are repeated after cold load. It first
fails ten assertions, then passes38 after forwarding the stored policy. The
removal/reuse regression passes252 assertions across four-/twenty-visit saved
continuations. Save133 and serialized layouts are unchanged.

A rejected fixture attempted to hold a wait with PauseUnit. Public pause retires
the current movement and starts a new task on resume; it cannot preserve that
wait. Its224 assertion failures describe a bad fixture assumption, not an engine
regression. The final test uses ordinary continuous owner ticks and respects the
allocator's cooldown.

## Frozen retail evidence

The existing ROUTE-05.1 research provides:

- three observed peer20 removal runs and an observer-free control;
- two observed requester4 far-replacement runs;
- one observed near-replacement run and an observer-free control;
- two genuine owned UI clicks per ordinary/Alt policy.

The strict verifier checks all ten complete observed streams, both public
controls and380 frozen owner-window records across five policy cases. Binary,
raw capture, metadata, public Preload and complete normalized-stream hashes are
pinned. The old owner-window fixture remains unchanged. UI repeats have different
absolute click clocks; the Alt repeat retains a known late one-ulp position
difference. Each raw numerical stream is pinned independently. There is no
observer-free UI capture, and the near-replacement case has one observation.
Those limits remain explicit rather than being normalized away.

```sh
python tools/ghidra/research/verify_yield_lifecycle162_live.py \
  --expected tools/ghidra/fixtures/retail-yield-lifecycle162-1.27.json \
  --captures /GitHub/wc3-analysis/reports/pathfinding-1.27/research/ROUTE-05.1/captures \
  --output /tmp/yield-lifecycle162-new.json
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test 'wc3_movement.selected_formation_yield_uses_saved_group_policy'
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test 'wc3_movement.yield_removal_reuse_preserves_wait_and_saved_countdown'
```

`route03_expected.py` retains raw numerical words while assigning process-pointer
roles. `route05_1_verify.py` verifies assignment, countdown, registry release and
replacement tokens. The strict wrapper checks completion before these older
helpers, whose generic loader alone skips observer errors. Raw maps, sources,
Frida captures, controls and failed UI attempts remain under
`research/ROUTE-05.1/` in the report root. Ghidra comments at168360,168070 and16dc70
are saved and mirrored in `MapPathfinding.java`; fresh assembly/decompilation and
engine red/green logs are under `/GitHub/wc3-analysis/runtime/payoff162/`.

This closes the identity/group-flag/countdown contract. Complete composed engine
trajectories for the UI witnesses, larger asymmetric encounters and yield chains
remain ROUTE-05.2 and the E2E tasks. See the
[TODO ledger](retail-pathfinding-todo.md#route-05--yielding-lifecycle) and
[engine evidence ledger](retail-pathfinding-engine.md#moving-blocker-group-policy-and-reuse-payoff162).
