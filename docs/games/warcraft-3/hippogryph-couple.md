# Hippogryph Couple (Mount / Pick up Archer / Dismount)

## Contract

`Acoa`, `Acoh`, and `Adec` are one mount/dismount system. They are **not**
Defend (`Adef`), Raven Form, or cargo load. TFT class names:

| Rawcode | Archive | Class | Parent | Role |
| --- | --- | --- | --- | --- |
| `Acoa` | ROC+TFT | `CAbilityCoupleArcher` | `Acou` | Archer mounts a partner unit |
| `Acoh` | ROC+TFT | `CAbilityCoupleHippogryph` | `Acou` | Partner picks up an Archer |
| `Adec` | TFT only | `CAbilityDecouple` | `AAsm` | Rider splits back into two units |

`Acou` (`CAbilityCouple`) is an abstract parent with no AbilityData row. Do not
register it.

Orders (Hive / `OrderIds`): `mounthippogryph` (`Acoa`), `loadarcher` (`Acoh`),
`decouple` (`Adec`).

### Authored fields (TFT)

| Rawcode | DataA | DataB | UnitID | Rng | Cool | Cost |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| `Acoa` | `ehip` (partner type) | — | `ehpr` (rider) | 100 | 0 | 0 |
| `Acoh` | `earc` (partner type) | — | `ehpr` (rider) | 100 | 0 | 0 |
| `Adec` | `earc` (companion 1) | `ehip` (companion 2) | — | 0 | 30 | 0 |

ROC `AbilityData` keeps `Acoa`/`Acoh` string rows but leaves `DataA`/`UnitID`
empty; `Adec` is absent. Empty UnitID must fail the mount (do not invent
`ehpr`). Stock ubertips still say “cannot dismount”; that text predates TFT
`Adec` on the rider.

`targs` is empty (`_`). Companion gating is ability-owned: same-owner living
unit whose `class_id` equals authored `DataA` (via `S_SpellDataId`).

### Couple Instant (out of scope)

`Aco2`/`Aco3` share `code=Acoi` (`CAbilityCoupleInstant`, parent `AAcs`):
area auto-find, `DataB` move-to-partner flag, large range. Keep the existing
`Acoi` stub; do not treat them as `Acoa`/`Acoh` aliases.

## Data Flow

```text
Acoa/Acoh AbilityData
  -> DataA partner fourcc, UnitID rider fourcc, Rng
CAbilityCoupleArcher / CAbilityCoupleHippogryph
  -> A_VALIDATE: friend + alive + class_id == DataA + UnitID present
  -> A_EXECUTE: SP_SpawnAtLocationNoBirth(UnitID), G_FreeEdict both inputs
Adec AbilityData
  -> DataA/DataB companion fourccs
CAbilityDecouple
  -> A_EXECUTE: spawn DataA + DataB at rider origin, G_FreeEdict rider
```

Merge consumes **two edicts** and creates a **third** (Warsmash CoupleInstant /
retail UnitID). Dismount is the inverse. Facing copies from the caster/rider.
HP blending and selection-replacement polish are not required for the first
contract.

## Registry rows

```c
{ "Acoa", CAbilityCoupleArcher, AB_SPELL, SPELL_TARGET_UNIT },
{ "Acoh", CAbilityCoupleHippogryph, AB_SPELL, SPELL_TARGET_UNIT },
{ "Adec", CAbilityDecouple, AB_SPELL, SPELL_TARGET_NONE },
```

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Acoa
build/bin/ability_audit -data 'data/Warcraft III' -raw Acoh
build/bin/ability_audit -data 'data/Warcraft III' -raw Adec
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.hippogryph_couple*'
```

Focused tests cover procedure registration, authored DataA/UnitID/DataB
mapping with non-stock fourccs, successful mount/dismount, and rejects for
enemy, dead, and wrong-type partners without mana spend.

## Original target-order producer and lifecycle

The 1.27.1.7085 original DLL (SHA256
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`)
provides a narrower, directly measured target-coupling contract. The current
simple-spell implementation above does not yet implement this paired command
owner. Public target names fail dispatch, and the UI moves only the initiating
actor. The private replacement is not part of full checkpoint175.

Stock availability depends on archive precedence. Base `war3.mpq` lists
`earc=Acoa,Ashm` and `ehip=Acoh`; expansion/patch lists use `Aco2`/`Aco3`
(`code=Acoi`) instead. `Reht` does not add missing abilities. Base RoC blank
DataA/UnitID cells must not be confused with the effective patched rows.
The accepted native probes explicitly add `Acoa` and `Acoh` through JASS,
set Reht1, and override only their rank-one ranges to333. The rejected natural
stock setup is preserved separately. This evidence concerns legacy/custom
explicit target coupling, not the patched-stock instant family.

| Entry | Numeric order | Native behavior |
| --- | ---: | --- |
| `mounthippogryph` | 852143 | Archer initiates |
| `loadarcher` | 852142 | Hippogryph initiates |
| Internal reciprocal | 852141 | Other actor targets the initiator; no public name asserted |

Producer `6f4e3c60` records the partner identity and creates target tasks
`d018a` and `d0171`, each with literal zero range, then submits the reciprocal
order. It ignores the reciprocal admission result. Both actors approach even
at initial separation256 with authored range333. Ready callback `6f4ef6f0`
requires two distinct ready owners before merge; authored range is checked at
completion rather than passed as the approach radius. Admission `6f4beda0`
requires the same owner, authored partner type, movement ability and reciprocal
coupling ability. Directional alliance alone is insufficient.

Readiness evidence184 preserves the scoped selector/merge decompilation and
capture rows in
[`coupling-readiness`](../../../tools/ghidra/fixtures/research/coupling-readiness/).
In accepted producer scene 0, Archer/Hippogryph collision radii are 32/8 world
units and maximum velocities are 270/400, respectively; both publish an arrival
range of 1.25 fine units. These measured inputs must be retained when comparing
ready timing. Selector `6f5fda10` dispatches `d0171` and `d0174` to `6f5ff8b0`
with the same nonpersistent argument and a different final subscription/validation
argument; this establishes no arrival-radius difference between the selectors.
Merge `6f4c2f20` reads replacement UnitID at the rank of the incoming, last-ready
ability. Its separate position/facing selection chooses the Hippogryph, so that
anchor does not imply that the Hippogryph ability supplies UnitID. This is native
evidence, not a completed implementation or trajectory-parity claim.

Cancellation `6f4bd000` clears the local partner/ready state and invokes the
linked peer inverse `6f4e4250`, which submits Stop851972. Measured outcomes:

- Stop or removal of either actor stops the surviving paired approach.
- Replacement Move survives on its actor and stops the peer.
- Pausing the initiating actor cancels the peer; resume reconstructs the public
  order and reciprocal order, and the pair merges.
- Pausing the reciprocal actor cancels the initiator; resume replays only the
  internal order. That actor approaches and waits alone through measured age348.
  The bounded observation does not prove permanent waiting.
- Stop from either actor after the first ready callback cancels the pair.

The public event probe directly observes issued-target publication for852141.
Peer Stop publishes its issued-immediate event before the explicit actor's Stop
notification. A nested Move from the reciprocal target event suppresses that
actor's coupling constructor while leaving the initiating order admitted;
a nested Move from the peer Stop event also survives. These are the two measured
callback cases, not a proof of all reentrancy. Trigger getters are read before
nesting; restoration of outer trigger-response context was not measured. A
reported target role0 means neither watched actor, not necessarily a null target.

A separate original-actor probe185 establishes the distinction: in the peer's
Stop callback, a Move issued to the original Archer is accepted, then superseded
by the outer Archer Stop. The final head is zero and the nested Move causes no
position change. This repeats across two observations and an unhooked control
(366 matching public markers). Do not generalize peer-directed nested Move
survival to the original actor or change common order precedence on that basis.
A RemoveUnit attempt at this frontier leaves the original type, life and pose
unchanged through the bounded capture. This does not establish successful
removal or explain why it did not occur; engine lifetime checks remain necessary.

Portable producer/lifecycle specs and compressed captures are
`tools/ghidra/fixtures/retail-coupling-{producer,lifecycle}-1.27.json[.gz]`.
The corresponding `tools/ghidra/research/coupling_*_evidence.py` auditors check
source/map pins, actor incarnations, task callers/ranges, two-ready ordering,
trace footers and exact public streams against an unhooked control. Producer
has999 accepted markers and a separate999-marker rejected setup; lifecycle
has1791 matching markers. Events180 adds711 matching markers. Each set contains
two observations and one control. Inherited `itemdrop_*` source filenames and
`TARGET-01.2-militia` metadata labels are preserved literally; coupling map,
source hashes and `COUP` markers identify the actual experiment.

Hook ticks are the last public marker, not exact event times. These corpora
remain `engine_parity:false`: no complete trajectory, HP transfer, selection
replacement, dismount or simultaneous configured-alias policy is certified.
See [producer inventory](retail-pathfinding-engine.md#coupling-target-command-producer).
