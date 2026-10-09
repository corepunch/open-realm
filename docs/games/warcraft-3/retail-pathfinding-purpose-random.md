# Game-owned purpose RNG state

Payoff172 closes NUM-04.6's state ownership and seed-side-effect contract. Retail
`CRandData` contains **45 game-purpose generators**, not a generator per unit.
The engine now keeps their 360 bytes once in `level.purpose_random`, separately
from the path/race/public-query owner. No per-entity allocation or initialization
is added.

## Producers and lifetime

Retail `TlsSlots_Get(0xd)` resolves the game-thread registry; entry3 of its data
array owns the364-byte `CRandData` object (vtable plus45 two-word states).
`65c7c0` constructs45 zero-seeded states, `693690` creates the entry during game
setup, and `693620` releases it on teardown. Engine level release clears the
purpose array; the actual loading-frame regression observes all45 zero states
before config/main after each real map reload.

`693710` seeds a local generator with its input, then seeds each purpose with
one successive local draw. Startup `29e300 →64f9a0 →693710` receives the same
fixed `0x77617233` or stored setup seed used by the path owner. Resolving0 or12
random races cannot change purpose states: those draws consume a different
owner. Production tests compare all45 words for eight original seed inputs,
with both fixed and all-random player preferences.

Public `SetRandomSeed` has a different seed chain: seed the path owner with the
requested word, consume exactly one owner draw, then use that result as the
`693710` input. Four complete original native bodies provide the regression
vectors, including zero and full-width inputs. The former `srand(input)` side
effect is removed. Public JASS tests verify libc continuation is unchanged;
presentation/audio are neither reset by this native nor put in the path owner.

Format139 saves the two logical words of every purpose using a typed array
schema. Existing save-version rejection applies. Load restores the current
positions, without reseeding or rerunning startup. A regression advances every
purpose a different number of times, saves, and compares all45 restored states
and their next results. A separate public item-query continuation crosses a
real save/load with the JASS VM and result hashtable intact.

## Indexed consumers

`693660` returns unsigned multiply-high of a full32-bit draw and the span.
It consumes a draw even for span0/1; `GetRandomInt`'s equal-bounds shortcut is a
different contract. The common helper preserves that distinction. The34
original range/unit-real vectors compare result bits, resulting state, and
noninterference with every other purpose and the path owner.

`ChooseRandomItem` and `ChooseRandomItemEx` share purpose35. The existing authored
`ItemData.pickRandom`, level/class filters and candidate order remain the source
of selection. Counting candidates consumes no draws; an empty candidate set
returns0. Nonempty selection now uses the indexed helper rather than libc
modulo. Actual JASS calls in the two-candidate fixture advance only purpose35,
produce the captured multiply-high choices, and resume identically after save.

This chunk provides the owned state and seed chain needed by the remaining
consumers. It **does not claim** that the engine's other random ability/native
consumers are all migrated or retail exact. In particular, the existing creep
and neutral-building selection stubs, attack attachment/reaction timers,
Gaussian dice above16, and unidentified bounce/drop owners are not implemented
by this chunk. Audio/animation remain separate presentation work; no generator
has been assigned to movement by assumption.

Fresh inspection also corrects the handoff's attack-attachment description:
`4993b0` schedules `base × StreamUnitReal(2)`, not an additive jitter. Saved
Ghidra annotations and the mapper preserve the correction; the recorded
one-draw-per-attachment evidence is unchanged.

## Evidence and reproduction

The [NUM-04.6 handoff](retail-pathfinding-handoffs/NUM-04.6/HANDOFF.md) contains
TLS/layout, calling conventions and39 statically classified stream indices.
The frozen research fixture preserves all observations and distinct combat
capture tails. `verify_purpose172_random.py` executes56 fresh original-code
calls, compares every oracle word, and reconstructs all530 purpose and1611
path-owner draws in the ten previously captured startups. Four observer-free
locked controls remain equal. The loading-only `mixed-observe-2` run is retained
as incomplete; no new live capture or unlocked observer-free control is claimed.
The oracle's only stand-in returns a synthetic TLS registry; generator/scalar
arithmetic runs original code.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_purpose172_random.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27/research \
  --output /tmp/purpose-random172.json
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 build/bin/openwarcraft3-tests \
  -data build/tests +dedicated 1 +test 'wc3_random_streams.*'
```

Production regressions failed on2032/2150 assertions before the implementation.
Classic/TFT each pass305 focused tests /393443 assertions across purpose RNG,
actual startup, public random natives, save/load and pathfinding. The five new
purpose tests contribute5318 assertions. Production/test builds and strict fresh
corpus verification pass. Local evidence:
`/GitHub/wc3-analysis/runtime/payoff172/acceptance-final.json`.
