# Initial alternate-form ownership

Adding a Morph ability to a unit already using its alternate endpoint publishes
the completed subtype before `UnitAddAbility` returns. Original `55d380` wraps
`55cce0`/`569f40` in the initial-alternate flag80000. The shared transition body
then calls completion synchronously, without acquiring Unit+198 or scheduling a
transition request. The current type and public order remain unchanged.

Corporeal Form first acquires its ordinary attachment source, then releases it
through that immediate completion. Ethereal Form acquires its alternate source.
Submerge publishes hidden state and its category inverse immediately. These
owners survive type refresh and cold save without repeated acquisition. Reverse
orders subsequently use the ordinary authored timed transition.

A positive `HeroDur` still schedules form expiry during initial alternate
completion. The common Morph owner retains that expiry, including its serial and
absolute deadline across type refresh and save/load. Expiry enters the normal
reverse transition. It is separate from normal zero-duration order delivery,
which acquires suppression and queues completion at the same primary clock.

Alternate364 reconstructs two public maps from the frozen separation input. Both
cover Corporeal/Ethereal and all three Naga Submerge variants. Two independent
observations and an unhooked control match422 public markers and854 native rows
for the stock lifetime map, and417 markers and849 rows for the positive initial
lifetime map. Every native row, canonical identity, primary-clock word, flag,
configuration and order is literal after removing only observer sequence and the
already identified presentation-effect timer. The verifier checks614 instruction
guards from11 complete functions; Ghidra annotations are saved.

The production regression uses non-stock endpoints and timing in both AbilityData
column schemas, exercises immediate attachment state, ordinary reverse, automatic
expiry, cold saved attachment and saved inverse. It first failed40 of302
assertions per edition. The positive-lifetime extension then failed26 of532
before the shared initialization owner scheduled expiry. Fourteen rejection tests
protect the frozen evidence. Timer assertions inspect the concrete Morph owner,
so another unit's earlier primary request does not replace the expected deadline.
The isolated indexed-source run passes19 tests and3049 assertions in each edition;
the Alternate364 regression itself passes550 assertions per edition.

Reports and baseline logs live under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-01.2/alternate364/`.
This work adds no saved fields. It covers these initial alternate producers;
Phoenix, Burrow invisibility, unrelated Morph subtype initialization and broader
counted-owner compositions remain within the existing SEP-01.2 audit.
