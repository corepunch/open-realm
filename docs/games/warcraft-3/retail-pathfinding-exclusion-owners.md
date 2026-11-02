# Current oracle exclusion ownership

BASE-03.3 is complete for the current corpus: **280 oracle contracts**, of which
233 contain **722 exclusion statements**, each assigned to exactly one of
24 remaining tasks. The other 47 oracles have empty exclusion lists. There are
zero unassigned statements and no new TODOs. This closes the assignment audit;
it does not resolve those statements or certify broader engine parity.

The [inventory](../../../tools/ghidra/fixtures/retail-pathfinding-exclusion-owners-1.27.json)
retains each oracle ID, zero-based exclusion index, exact original wording,
primary owner, disposition and reason. Compound statements have one accountable
primary owner; that owner must reconcile their components with the related
inventories when resolving them. No original corpus exclusion was edited.

Scope is `kind == "oracle"`, including names such as `shared-pair` and
`motion-engine-exact`, not merely IDs starting with `oracle-`. Capture entries
are outside this particular assignment audit. The checked oracle ID list also
includes contracts with no exclusions, so adding an oracle cannot silently
bypass review.

## What the assignments mean

- **361 behavior statements** go to the relevant movement, route, target, group,
  numeric, order, exclusion-scope, separation or composed-scenario task.
- **102 reachability statements** go to the public entry/branch or valid-input
  inventory. Seeded handles, allocator capacity, virtual endpoints and forced
  flags remain supplied inputs until their producers are established.
- **183 evidence boundaries** go to repeated-scenario review or the frozen
  replacement contract. These retain observer limitations, precision, engine
  versus retail storage distinctions and explicit out-of-scope claims. They do
  not imply that private native addresses or renderer timing must be copied.
- **76 historical/superseded statements** go to E2E-05.2 for an evidenced
  disposition. For example, old exclusions still name BASE-06.4, MAP-01.2/3,
  NUM-01.15, ORDER-04.2 and TARGET-03.2 after those tasks closed. Later evidence
  may discharge a gap, but it does not retroactively expand an earlier oracle.
  A historical reference to a closed task is therefore not used as its owner.

E2E-05.1 still owns joining this exclusion inventory with the larger branch,
field, stub and tolerance inventory. READY-01.1 still requires actual evidence
or proved unreachability for every in-scope gap. Neither task is closed by this
assignment pass. SEP-01.2 also remains open despite the independently tested
morph/channel/teleport/cargo/AI fixes.

## Validation and maintenance

From the repository root:

```sh
python3 tools/ghidra/audit_wc3_pathing_exclusion_owners.py --report /tmp/exclusion-owners.json
python3 -m unittest discover -s tests -p test_wc3_pathfinding_exclusion_owners.py
```

The verifier compares the inventory with the current corpus and open checklist,
rejecting missing/duplicate/changed exclusions, omitted oracles, closed/unknown
owners, self-assignment to BASE-03.3, missing rationales and invalid dispositions.
Twelve tests include deliberate corruptions of those contracts. The report
contains exact per-owner and disposition counts; it executes no game or native
oracle and makes no new retail behavioral claim.

When a corpus exclusion changes, update its assignment in the same change.
Before closing an owner, either assign its still-current exclusions to another
appropriate remaining task or update the corpus with the evidence-backed
resolution and adjust this inventory. Do not keep a closed owner merely to make
an old count pass, and do not replace a behavior owner with a generic acceptance
gate to hide unfinished work.

ROUTE-04.1 closure335/336 adds three explicitly owned evidence boundaries and
moves its three original limitations to E2E-05.2's superseded-evidence review.
Their original text is unchanged; the disposition names the new public cache,
engine motion and cold-save proof. This records completed route-mode work,
not a transfer of unfinished behavior to a generic acceptance gate.

ROUTE-04.2 closure337 reconciles its24 original exclusions against the later
arrival, target and public portal lifetimes. Twenty retain their exact text as
superseded-review assignments; four “whole retail pathfinder” exclusions remain
explicit E2E evidence boundaries. The new337 oracle adds two boundaries for
controlled-state versus public production and engine-save versus retail-save
coverage. See [arrival composition](retail-pathfinding-routes.md#arrival-cancellation-and-route-mode-composition-payoff337).

MAP-04.2 coverage342 reconciles its34 original statements individually against
the complete exit/restoration table. Seventeen retain superseded-review ownership,
four retain explicit evidence boundaries, and thirteen remain behavior work with
existing GROUP-04.6, ORDER-01.13/01.16, FOOT-04.2 and SEP-01.2 owners. Each reason
is recorded in `retail-map342-coverage-1.27.json` and the ownership inventory.
No original exclusion text or expected retail fixture is changed. The remaining
behavior assignments concern surrounding producers and lifecycle semantics;
the scope-restoration obligations are covered by the joined native and engine
checks. See [the scope table](retail-pathfinding-exclusions.md#exit-and-reentrancy-coverage-audit342).
