# Authored movement profiles

Payoff131 closes BASE-02.1's authored producer, lane, query/category and support
source inventory. The engine now uses one immutable profile table for authored
classification. Positive-speed units with a zero fine query can move; `unbuild`
units publish category08. Fine queries and coarse hierarchy selection are separate
inputs, including when Ensnare changes an authored flyer's physical layer.

## Original producer and complete table

For game.dll1.27.1.7085 (`d51e5680…8236`), `6b3640` reads UnitData `movetp`.
The profile-builder slice `66c909..66c944` calls `685340`, stores movement bits
at row+1a8, then calls `685e30` with EDX1/0 for category+1ac/query+1b0.
`685db0` maps those exact values to movement-class flags; publication shifts
those flags right by one. This is a scalar switch, not a priority over set bits.

The parser uses the original Storm `SStrCmpI` and shipped CRT `_strnicmp` with
the captured default locale. Comparisons are case insensitive and exact through
the first NUL. `FOOT`, `fLy` and `Float` work; surrounding spaces, combined names,
unknown strings, `_`, `-`, `none`, empty and null inputs produce zero. The complete
oracle retains60 nonnull strings,135 bit inputs/810 mapper calls and60 builder
executions. It changes only the builder's SLK getter, supplying the string.

| Authored name | Bits | Fine query | Occupied category | Coarse class | Native packed coarse mask | Support source |
|---|---:|---:|---:|---:|---:|---|
| foot |01|02|ca|0|06000006|Terrain or strictly higher deck, plus eligible fly offset |
| horse |04|02|ca|0|06000006|Same ground source |
| hover |08|02|ca|0|06000006|Maximum of base support and water, plus authored moveHeight |
| fly |02|04|00|3|04000004|Base support plus interpolated offset and layer−3 blend |
| float |10|40|ca|2|40000040|Maximum of base support and water |
| amph |20|80|ca|1|80000080|Water maximum only when the previously published deep-water flag is set |
| unbuild |40|00|08|0|06000006|Ground source |
| none, empty or unknown |00|00|00|0|06000006|Ground source |

Values are hexadecimal. The engine's ground lane selector02 selects its existing
06 reducer; it does not replace the native coarse ground mask with a fine query.
The table's support policy identifies the branch. Complete numerical support,
cliff, water and bridge fixtures remain MAP-02.2; the existing engine support
implementation still needs the captured amphibious refresh lag, deck rules and
flyer blend. This inventory does not certify those numerical implementations.

`66d780` selects support, `68f390` supplies the fly offset, `64eca0` supplies deep
water and `684480` publishes the refreshed position. Retail obtains support before
refreshing the amphibious deep-water flag. Ground fly-height getters and support
are also distinct: an ordinary foot unit can report a changed GetUnitFlyHeight
without raising its support, while adding then removing Amrf retains the flag
that makes the offset apply. The original research witnesses and support state
words remain frozen, including morph landing's temporary profile/class split.

## Mobility and engine data flow

Zero query/category is not absence of a Move ability. The captured creation speed
at+68 controls the implicit owner: original `68a8c4..68a8dc` skips factory65bd60
only for positive or negative zero. Sixteen complete instruction-slice cases
include negative values, subnormals, infinities and unordered NaNs. No movetp
check exists in that branch. `685310` reads Unit+1ec and returns zero when the
Move owner is absent; `698af0` similarly ignores a setter without that owner.
Move's own disable counter is a separate effective-speed gate.

`wc3_pathing_profile.h` contains shared immutable type facts. Metadata compilation
resolves the authored name once, and the ordinary hot path indexes the table.
Move owns fine query/category, mobility and coarse selector accessors. Spatial
publication and fine occupancy consume the occupied category, even for an
authored foot unit without a Move owner; member/group route
queries explicitly carry the coarse selector. Portal placement and formation
queries also use the coarse selector while fine admission keeps its own query.
No rawcode-specific cases, new per-unit allocations or deferred work are added.
The extra route-query field is temporary; persistent unit and save layouts are
unchanged.

A real Ensnare cast now checks fine02/categoryca and coarse air04 together. The
research traces establish that forced grounding does not republish class3. Full
Crow-form delayed landing/rebind, Burrow/Submerge and other runtime producers are
retained evidence, not newly certified engine implementations by this change.

## Controls and regression evidence

The original types2 first/repeat captures contain24 births and574 public markers;
the observer-free control matches every marker. The frozen BASE payload remains
byte-identical (`32ee7101…3b5d5`). Failed types-v1 spell setup is recorded in the
handoff and excluded from its runtime-producer conclusions.

A separate movement-only map creates15 friendly, invulnerable custom units,
disables acquisition and orders all units at tick10. First and repeat agree on
all births,852 parser events and362 ordered normalized events. The observer-free
run agrees on272 public markers. All seven positive-speed zero-query variants,
including unbuild, accept Move and translate. Float on land remains stationary;
this is preserved evidence, not a test failure hidden by excluding that type.
R2S positions have three decimals; these witnesses do not claim word-exact
complete movement trajectories. Support callback counts vary with presentation
sampling and are not used as a movement timing oracle.

The new native-created engine regression failed13 of72 birth assertions before
the implementation. The completed tests cover the full parser/switch input set,
18 actual native births, live fine occupancy, retained instance owners,
zero-query public movement and Ensnare. `retail_movement_profiles.h` contains every original bit-switch row.
The blocked-walk save fixture now authors Hpal movetp=foot, verified against
the installed retail UnitData row; it previously relied on the erroneous
empty-name ground fallback. Focused Classic and TFT each pass580 tests/7,319,016 assertions across profiles,
pathfinding, movement, Ensnare and save/load. The fresh strict original/C verifier
passes617 cases;31 Python checks cover evidence, exclusions and corpus integrity
(363 entries/488 hash pins). Full repository validation follows the established
batch cadence. Logs: `/GitHub/wc3-analysis/runtime/payoff131/`.

`verify_wc3_pathing_profiles.py` regenerates the complete original report,
compiles the production header, reproduces the owner gate, reconstructs the
unchanged BASE freeze, compares both complete repeat/control sets and rejects
incomplete captures. The portable compressed bundle includes all four raw
captures, both control preloads, generated movement-map JASS, normalized events
and full reports. The legacy observer's metadata names its default probe source;
`profiles-map.j` is the actual movement-only map source retained in the bundle.

Ghidra is saved and read back with32 mapped functions, the12-byte partial authored
profile fields and three explicit ECX/EDX prototypes. `MapPathfinding.java`,
`retail-profile-types-1.27.json` and `retail-profile-ghidra-1.27.json` retain the
reusable mapping/schema and saved database metadata. No inferred full profile
layout or support prototype is assigned.

Reproduce the strict contract with:

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_profiles.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --report /tmp/wc3-profile-oracle.json
```

The binary requires its matching Storm.dll and msvcr120.dll siblings. The
[original handoff](retail-pathfinding-handoffs/BASE-02.1/HANDOFF.md) retains the
researcher's complete static/live provenance and runtime witnesses.
