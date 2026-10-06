# Warcraft III Demo and TFT Ability Class Registries

For translating the recovered behavior into C flags and explicit callbacks, see the
[flat C ability plan](ability-system.md). The proposed runtime does not reproduce the parent tree.
The [binary behavior inspection](ability-inheritance-binary.md) records constructors, virtual slots,
shared cast lifecycle, and the demo's absence of Holy Bolt.

## Extracting the reference

```sh
python3 tools/extract_wc3_ability_classes.py data/warcraft3demo/game.dll -o docs/games/warcraft-3/demo-ability-classes.txt
python3 tools/extract_wc3_ability_classes.py data/warcraft3demo/game.dll --all-classes -o /tmp/demo-all-classes.txt
python3 tools/extract_wc3_ability_classes.py "data/Warcraft III/Warcraft III.exe" --all-classes -o games/warcraft-3/tft-ability-classes.txt
python3 tests/test_wc3_ability_classes.py
```

The Python tool needs no third-party packages and does not execute the DLL. The
[generated reference](demo-ability-classes.txt) contains all **197 registered
`CAbility*` classes**, sorted by case-sensitive FOURCC, in `s_skills.c`-style rows.
Each row also records the parent type and file offsets of the registration call,
factory, destruction callback, pool cleanup callback, and RTTI name. `--all-classes`
exports all **526 static class registrations**, including buffs and other objects.
The complete saved registries live beside the game:
[demo (526)](../../../games/warcraft-3/demo-ability-classes.txt) and
[TFT (1,076 mappings plus four helper RTTI entries)](../../../games/warcraft-3/tft-ability-classes.txt).

This is the September 2002 demo, not TFT or a complete retail ability inventory.
The demo reader requires SHA-256
`286823c37a1083e91f07d040e46a9df7af4c4952e01fcbba460589bd4e297654`
(6,205,503 bytes), rejecting other builds before interpreting any offsets.
The local binaries are not redistributed as fixtures. Integration checks require both;
they verify the entire exposed ability RTTI inventory, known associations/offsets,
reproducible output, the all-class mode, and rejection without output corruption.

## Abilities versus buffs

A **buff** is an effect attached to a unit that changes its state or stats, often
for a limited duration. Beneficial effects are commonly called buffs and harmful
effects debuffs; Warcraft III's `CBuff*` classes represent both. A buff is gameplay
state, separate from the icon or visual effect used to present it.

An ability owns the action that applies an effect; a buff represents the applied
effect on the affected unit. Bloodlust illustrates the distinction with two
separate registrations in this demo:

| FOURCC | Class | Role |
|---|---|---|
| `Ablo` | `CAbilityBloodlust` | The ability that casts Bloodlust |
| `Bblo` | `CBuffBloodlust` | The Bloodlust effect applied to the target |

An aura follows the same general pattern: the source unit's ability affects
nearby eligible units, while buffs represent its effects on those units. The
ability and its applied buffs have separate lifetimes; an aura need not have a
fixed duration.

The default extractor output selects names beginning with `CAbility`, so it
includes `Ablo` but excludes `Bblo`. `--all-classes` includes both, along with
other registered game objects. These retail class names describe the demo's
organization; they do not require matching C++ classes in our C implementation.
See [ability, buff, and item presentation effects](ability-and-item-effects.md)
for the separation between gameplay state and its visual presentation.

## Registration mechanism and offsets

There is no flat, on-disk `abilitylist[]`. Startup code builds a runtime class
registry by calling **VA `0x6F073450`, file offset `0x00073450`**. In this build,
the preferred image base is `0x6F000000`. `.text`, `.rdata`, and file-backed `.data`
have equal RVAs and file offsets; this is not true of every PE section or DLL.
The extractor scans `.text` at file offsets `[0x1000, 0x4EB0E8)` and reads names
from file-backed `.data` at `[0x546000, 0x59C000)`.

The 25 bytes immediately before each static registration call encode:

```asm
push pool_cleanup
push destroy
push create
mov  edx, parent_fourcc
mov  ecx, type_fourcc
call 0x6F073450
```

The register function hashes the type key, adds/updates its record, links it to
the parent record, and stores the three stack arguments. The registry owner
pointer is at VA `0x6F5B0344`; its type hash table begins at owner `+0x04`.
The type record uses `+0x18` for the key, `+0x70` for the factory, `+0x74` for
destruction, `+0x78` for pool cleanup, and `+0x7C` for the parent type ID.
These are runtime record offsets, not a serialized table in `.data`.

All 526 pool cleanup callbacks begin `push -2; push rtti_name`. Their factories
also pass that identical RTTI name to the allocator within the first 24 bytes.
The extractor cross-checks both references and reads `.?AV<Class>@@` directly.
The two other calls to the register function (file offsets `0x712BF`, `0x712DF`)
obtain agent-root type IDs through `0x6F07EBB0` (returns `+aga`); the registered
types are `+aga` and `+agr`, not abilities.
Every call to the register function is accounted for: 528 total, 526 static, two
explicit dynamic root calls. Unknown call shapes or incomplete counts are errors.

For example, the Aura registration starts at file `0x221C70`:

```asm
push 0x6F221D70        ; pool cleanup
push 0x6F221D50        ; destruction
push 0x6F221C90        ; factory
mov  edx, 0x41426F6E  ; ABon (CBonusBase)
mov  ecx, 0x61757261  ; aura
call 0x6F073450       ; call instruction at file 0x221C89
```

Both factory and pool cleanup reference `.?AVCAbilityAura@@` at file `0x56DE30`.
The adjacent plain name `CAbilityAura` is at `0x56DE44`, returned by the small
function at VA `0x6F221D40`. FOURCC immediates have reversed bytes on disk:
`aura` is numeric `0x61757261`, stored as bytes `61 72 75 61` (`arua`).

## Limits and misleading approaches

- **`ACat` is absent from this DLL**, in both byte orders. The actual registration
  is `"aura" -> CAbilityAura`. The output is a type registry: internal base types
  such as `abil`, `AAsp`, and `aura` are included. Expanding object-data ability
  IDs requires separately establishing their relationship from the matching game
  data; this tool does not guess aliases or substitute a superclass.
- Names alone do not determine the registered ID. This DLL registers
  `AOws -> CAbilityStomp` and `Awar -> CAbilityWarStomp`.
- `CAbility*` is an explicit output filter, not the complete inheritance subtree:
  `CBonusBase`, `CPower`, buffs, and fire effects also descend from abilities.
  Use `--all-classes` to inspect them and their parent IDs.
- `tools/ability_map.c` has a manually transcribed `classmap[]`; it only validates
  class-name presence against RTTI. Its mappings are not extracted call-site
  evidence. This extractor does not use that table or modify gameplay handlers.
- RTTI descriptors at `name - 8` need not have conventional MSVC RTTI cross
  references here. The allocator callbacks reference the name bytes themselves.

## Full TFT executable comparison (1.29.2.9231)

The local `data/Warcraft III/Warcraft III.exe` contains the game implementation
and exposed ability class names too. Its embedded `VS_FIXEDFILEINFO` at file
`0xD3DCB8` reports **1.29.2.9231**. This is a later full-game build, so differences
from the September 2002 demo include both expansion content and later patches.

| Property | Full executable |
|---|---|
| Size | 14,854,632 bytes |
| SHA-256 | `a1950f17905b9cd7d5461d45e6723af36dda5304e12dba27a1fea85593b15f3f` |
| Format / preferred image base | x86 PE32 / `0x00400000` |
| Unique `.?AVCAbility...@@` RTTI names | 493 |
| Demo RTTI names retained | All 197 |
| Additional RTTI names | 296 |

The 493 count is a **class-name inventory, not a verified registration count**:
it includes helpers such as `CAbilityDatabase`, `CAbilityCustomData`,
`CAbilityMetaDB`, and `CAbilityDB`. Retaining a class name does not establish
unchanged implementation behavior or the same registered ID across versions.

Direct class-name and FOURCC getters confirm these sample associations. Addresses
in this table are preferred **VAs**, not file offsets:

| FOURCC | Class | Name getter VA | FOURCC getter VA |
|---|---|---|---|
| `aura` | `CAbilityAura` | `0x00B8F800` | `0x00B8F810` |
| `ANcl` | `CAbilityChannel` | `0x00C96AD0` | `0x00C96AE0` |
| `AEbl` | `CAbilityBlink` | `0x00C477E0` | `0x00C477F0` |
| `AEar` | `CAbilityAuraTrueshot` | `0x00C24340` | `0x00C24350` |
| `Arav` | `CAbilityRavenForm` | `0x00BB46A0` | `0x00BB46B0` |
| `Aply` | `CAbilityPolymorph` | `0x00B88660` | `0x00B88670` |

`ACat` is still absent as a literal in both byte orders; do not infer its
object-data alias relationship solely from the presence of an Aura class.

The registration mechanism has changed. Aura registers at VA `0x00B90F50`
(file `0x790350`): it pushes descriptor address `0x011B5B0C`, calls parent-ID
getter `0x006F7A50` (returns `ABon`), pushes that result and `aura`, then calls
**register function VA `0x007F6360` (file `0x3F5760`)**. Channel follows the same
pattern at VA `0x00C998D0` (file `0x898CD0`), with descriptor `0x011B9648`,
parent getter `0x00B2B1F0`, and type `ANcl`.

Aura's factory/destruction/pool callbacks are VAs `0x00B90A10`, `0x00B90A80`,
and `0x00B90AB0`, stored consecutively at file `0xB35870`. Its factory and pool
cleanup reference RTTI name `.?AVCAbilityAura@@` at file `0xD1042C`
(VA `0x0111182C`). The Python extractor selects this descriptor-based reader by
the executable's SHA-256, independently of its filename; unknown builds are rejected.

For this executable, convert file offsets within `.text` to VAs by adding
`0x400C00`, within `.rdata` by adding `0x401200`, and within file-backed `.data`
by adding `0x401400`. Parse the PE section table for general tooling; do not reuse
the demo's assumption that RVA equals file offset.

### TFT extraction coverage

All **1,077 calls** to `0x007F6360` are accounted for. Of the 1,076 static
registrations, 706 obtain the parent ID from a `mov eax, immediate; ret` getter;
370 push the parent ID directly. Every descriptor has exactly one startup
`mov dword ptr [descriptor_va], vtable_va` assignment. The reader follows its
three-pointer vtable to the factory, destruction, and pool cleanup callbacks.
All 1,076 pool cleanup callbacks begin `push 1; push -2; push rtti_name`.
That RTTI reference supplies the class name without guessing from nearby strings.
Unlike the demo, some TFT factories use wrappers or thunks, so their first bytes
need not contain the same RTTI argument; the descriptor's pool callback is the
common naming source for this build.

The remaining call at file `0x3F49A9` registers unpooled root `+aga` with itself
as parent. Both IDs come from getter VA `0x007F2600`; its descriptor is at
VA `0x010DFE7C` and has an empty pool cleanup function without a RTTI argument.
It is explicitly excluded from the static pool-backed export and noted in its
header, analogous to the demo's two special root registrations.

There are **489 registered `CAbility*` classes**. The four additional RTTI names
`CAbilityDatabase`, `CAbilityDB`, `CAbilityCustomData`, and `CAbilityMetaDB` have
no registration in this factory registry. `--all-classes` exports all 1,076
pool-backed classes plus a separate appendix for these four helpers; without it,
the TFT reader exports the 489 registered ability classes.
TFT rows add `desc_va` (runtime VA), `table` (vtable file offset), and `init`
(startup assignment file offset) to the common callback/RTTI evidence columns.

The helper appendix is derived by scanning RTTI and subtracting the registered
class names. It retains the decorated name, RTTI file offset and VA, and MSVC
TypeDescriptor file offset and VA (the two-DWORD header eight bytes before the
name). Its rows use `NULL` for the FOURCC to indicate absence from this registry;
no parent, factory, or ability ID is inferred. The result accounts for all 493
`CAbility*` RTTI names without confusing the four helpers with castable abilities.

| Unregistered helper | RTTI file offset | RTTI VA |
|---|---|---|
| `CAbilityCustomData` | `0x00CFC47C` | `0x010FD87C` |
| `CAbilityDB` | `0x00CFFD1C` | `0x0110111C` |
| `CAbilityDatabase` | `0x00CFC3C8` | `0x010FD7C8` |
| `CAbilityMetaDB` | `0x00CFF2B8` | `0x011006B8` |

`python3 tests/test_wc3_ability_classes.py` checks both original binaries and
saved references, both CLI modes, getter/direct-parent registration examples,
the complete registered ability and helper RTTI inventories, helper descriptor
headers, and PE address translation including rejection of a zero-filled descriptor address.

See [ability implementation](ability-implementation.md#disassembly-optional-evidence)
for using disassembly as version-specific behavior evidence, and
[diagnostic tools](../../diagnostic-tools.md#warcraft-iii-demo-ability-class-extraction)
for the tool index.
