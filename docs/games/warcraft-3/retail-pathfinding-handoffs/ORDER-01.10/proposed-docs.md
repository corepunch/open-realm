# ORDER-01.10 proposed documentation (not applied)

## TODO (`retail-pathfinding-todo.md`, ORDER-01.10 line) — proposed note while open

Research handoff ORDER-01.10: repeated public retail scenes (two observed + observer-free control each) fix the
public heads 851983 (Attack target and Attack Move), 851984 (Attack Ground, including an accepted Footman order) and
851985 (attackonce), their completion, deferred retirement after KillUnit/RemoveUnit of the target (2–4 ticks, swing
wait d016a), replacement, Stop, death/reuse and Shift-queued handoff; unattackable targets (air, invulnerable, self)
are accepted and converted by the native dispatch 207160 into a point Attack Move to the target position. Internal
task chains never change the public head; automatic acquisition keeps 851983 (Attack Move) or 0 (idle). The former
"Move d0016 at 5fe1a0" starting point is the Patrol expansion, not Attack Move. Remaining: live save/load
(proposed ORDER-01.19) and AI-owned completion (proposed ORDER-01.20). [Handoff](retail-pathfinding-handoffs/ORDER-01.10/HANDOFF.md).

New items:
- [ ] **ORDER-01.19** Capture UI save/load of an active Attack, Attack Move and Attack Ground and of a queued attack
  order; assert the restored public head, remaining chain and next activation.
- [ ] **ORDER-01.20** Trace AI-owned Attack completion: `49e990` appends immediate d0006 when ability3c<=0 and
  Unit5c bit4; identify the bit4 producers and the public effect for computer-owned units.

## Ledger (`retail-pathfinding-engine.md`, "Remaining command-owner inventory")

Replace the ORDER-01.10 row text "Shared dispatch and Move d0016 at5fe1a0; distinguish public attack IDs from
automatic sub-behaviors" with: "CAbilityAttack vtable 6fadb5a4 dispatch 49a5f0: d000f target/point 49a980, d0010
49af00, d0011 49b0f0; native target dispatch 207160 converts invalid targets to points; public heads 851983/851984/
851985 are user-order commands; acquisition prepends internal sub-chains (L, handoff ORDER-01.10)."

Proposed new section "Attack ownership publishes the user order, not combat tasks": the behaviour table of the
handoff (15 cases), the ordered chains, E1–E7 and the deferred target-loss contrast with Follow (01.15).

## Corpus (`retail-pathfinding-corpus-1.27.json`) — proposed entry

```json
{"id": "live-attack-ownership", "kind": "live-frozen",
 "command": ["{python}", "tools/frida/research/order0110_verify.py", "--expected", "{fixture:ORDER-01.10-expected.json}", "--report", "{report}"],
 "expected_exit": 0, "expected_status": "verified", "evidence": ["L", "A"],
 "scope": "Public Attack/Attack Move/Attack Ground/attackonce heads, completion, target loss, replacement, Stop, death/reuse, queued handoff; 15 claims, 6 negative controls.",
 "exclusions": ["live save/load (ORDER-01.19)", "AI-owned completion (ORDER-01.20)", "combat numerics"]}
```
