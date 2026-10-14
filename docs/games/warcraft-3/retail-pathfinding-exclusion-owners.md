# Current oracle exclusion ownership

BASE-03.3 is complete for the current corpus: **279 oracle contracts**, of which
232 contain **720 exclusion statements**, each assigned to exactly one of
25 remaining tasks. The other 47 oracles have empty exclusion lists. There are
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

- **385 behavior statements** go to the relevant movement, route, target, group,
  numeric, order, exclusion-scope, separation or composed-scenario task.
- **102 reachability statements** go to the public entry/branch or valid-input
  inventory. Seeded handles, allocator capacity, virtual endpoints and forced
  flags remain supplied inputs until their producers are established.
- **177 evidence boundaries** go to repeated-scenario review or the frozen
  replacement contract. These retain observer limitations, precision, engine
  versus retail storage distinctions and explicit out-of-scope claims. They do
  not imply that private native addresses or renderer timing must be copied.
- **56 historical/superseded statements** go to E2E-05.2 for an evidenced
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
