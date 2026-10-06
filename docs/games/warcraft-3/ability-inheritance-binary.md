# Ability Inheritance: Demo and TFT Binary Evidence

Inspected 2026-09-12 to refine the [ability system plan](ability-system.md).
This is static analysis of local binaries, not recovered source or proof of complete behavioral parity.
Method names below describe observed operations; original method names are unavailable.

## Binaries and Method

| Binary | SHA-256 | Role |
|---|---|---|
| `data/warcraft3demo/game.dll` | `286823c37a1083e91f07d040e46a9df7af4c4952e01fcbba460589bd4e297654` | September 2002 demo; six base classes |
| `data/Warcraft III/Warcraft III.exe` | `a1950f17905b9cd7d5461d45e6723af36dda5304e12dba27a1fea85593b15f3f` | TFT 1.29.2.9231; Holy Bolt and constructor cross-checks |

The demo has no `CAbilityHolyBolt` RTTI, no registered `AHhb`, and neither byte order of the rawcode.
Do not present Holy Bolt observations as demo evidence. The user confirmed the `data/Warcraft III` executable;
there is no `reference/Warcraft III` directory in this checkout.

Followed registered factories to constructors, recovered instance vtable writes, compared parent/child slots,
and inspected selected methods with both radare2 `pdg` and `pdf`. Factory-registry callback tables are different
from instance vtables. All addresses below are preferred **virtual addresses**, and slot numbers are byte offsets.
Use the extractor's PE section conversion for file offsets; TFT does not have the demo's RVA/file identity.

The verified registered chain in both builds, for classes present, is:

```text
HolyBolt -> SimpleSpell -> Spell -> Button -> Power -> Interfaced -> Ability -> uref
AHhb        AAsm           AAsp     AAbt      powr     AAin          abil
```

## What Each Layer Contributes

| Class | Observed contribution | Representative evidence |
|---|---|---|
| `CAbility` | Common ability identity, owner/reference and lifecycle plumbing, data loading, level-indexed values and default virtual operations | Demo factory `0x6F2BEFC0`, vtable `0x6F504810`; loader `0x6F2BF1E0` |
| `CAbilityInterfaced` | Adds a common interface registration/removal hook, invoked by lifecycle methods | Demo vtable `0x6F506178`; `0x6F256A30`, `0x6F256A50`, `0x6F256A60` call slot `+0x1E8` with 0/1 |
| `CPower` | Adds order-validation entry points returning result codes, followed by a common availability check | Demo vtable `0x6F502A98`; `0x6F2BE8D0` through `0x6F2BE990` dispatch slots `+0x1EC` through `+0x1F8`, then `+0x194` |
| `CAbilityButton` | Implements order registration and command-button construction, order IDs, command flags, alternate on/off command handling | Demo vtable `0x6F503990`; `0x6F235450`, `0x6F235210`, `0x6F2355A0`, `0x6F2353C0` |
| `CAbilitySpell` | Shared spell readiness checks, cast lifecycle/event handling, timed revalidation, effect-phase dispatch, cooldown-related data and cleanup | Demo vtable `0x6F504300`; `0x6F2BD500`, `0x6F2BD680`, `0x6F2BD720`, `0x6F2BDED0` |
| `CAbilitySimpleSpell` | Adds unit/widget/point target validation, retained targets/coordinates, construction of cast orders, target revalidation and target-specific effect dispatch | Demo vtable `0x6F50AAA8`; `0x6F2BAE30`, `0x6F2BB560`, `0x6F2BBC50`, `0x6F2BC180` |
| `CAbilityHolyBolt` | Supplies concrete identity/order properties, unit validation and healing/damage effect, plus specialized lifecycle overrides | TFT vtable `0x00F74E64`; validator `0x00C0C400`, effect `0x00C0C430`, order getter `0x00C0CE70` |

Interpretation: Interfaced provides the lifecycle connection; Button implements concrete command registration and
presentation. Calling Interfaced a rendering/UI implementation would overstate the evidence. Power's validation
entry points are distinguished by their call arguments; exact original names and every result-code meaning remain unresolved.

### Common Data Starts at Ability

Demo `CAbility` loader `0x6F2BF1E0` calls helpers for authored values, including:

| Helper VA | Observed field strings |
|---|---|
| `0x6F2C0570` through `0x6F2C0660` | `Data11`/`Data21`/`Data31` through `Data14`/`Data24`/`Data34` |
| `0x6F2C06B0` | `Cast1`–`Cast3` |
| `0x6F2C0700`, `0x6F2C0750` | `Dur1`–`Dur3`, `HeroDur1`–`HeroDur3` |
| `0x6F2C07A0`, `0x6F2C07F0` | `Area1`–`Area3`, `Rng1`–`Rng3` |
| `0x6F2C0840`, `0x6F2C0890` | `Cost1`–`Cost3`, `Cool1`–`Cool3` |

This argues against treating authored spell data as a separate spell identity. It does not require copying the
demo's three-level arrays or field layout; retain our normalized DDX data and accessors.

### The Bases Call Concrete Hooks

Demo SimpleSpell adds these distinguishable operations:

- Slot `+0x290`, `0x6F2BAE30`: validates a generic target, checks common restrictions, distinguishes unit/other widget,
  and calls specialized validators `+0x294` or `+0x298`.
- Slot `+0x28C`, `0x6F2BB150`: point/destructible-shaped target validation; invokes `+0x29C` after shared checks.
- Slot `+0x2A0`, `0x6F2BB560`: stores target references/coordinates and builds an ordered cast sequence.
- Slot `+0x25C`, `0x6F2BBC50`: first calls Spell's `0x6F2BDB20`, then checks retained target/range again.
- Slot `+0x27C`, `0x6F2BC180`: selects unit, other widget, point, or no-target effect hooks at
  `+0x2B0`, `+0x2AC`, `+0x2A8`, `+0x2B4` respectively. Base concrete-effect hooks are no-ops.

Spell `0x6F2BDED0` checks timed conditions and calls virtual revalidation `+0x25C` before reaching effect dispatch
`+0x27C`; failure invokes termination `+0x268`. The effect call is followed by cooldown-related processing and
completion handling. `0x6F2BDD80` and `0x6F2BDED0` schedule lifecycle notifications through `0x6F0863E0`.
SimpleSpell overrides cleanup paths `+0x268`/`+0x26C` to clear retained target state and then invoke base machinery.

This is evidence for distinct acceptance, approach/cast, effect and termination phases. Exact Holy Bolt cast-point
timing, interruption refunds and every notification meaning are not established by these selected functions.
Do not change timing constants based only on this analysis.

### TFT Holy Bolt

Constructor evidence independently ties the classes to actual instance vtables:

- Holy Bolt factory `0x00C0CCD0` calls allocator/constructor wrapper `0x00C0D1A0`.
- That wrapper calls SimpleSpell constructor `0x00B2AC80`, then writes Holy Bolt vtable `0x00F74E64`.
- SimpleSpell calls Spell constructor `0x00B2AD50`; its vtable is `0x00F18508`.
- Spell's constructor installs Button vtable `0x00F18940`, then Spell vtable `0x00F18C98` while initializing
  additional state. Name getters verify the identities of these vtables.
- TFT Ability constructor `0x0046D010` installs `0x00E6DF38`. Interfaced and Power factories call that constructor
  and install `0x00F168F8` and `0x00F17318`, respectively. Their inspected factories add no explicit data initialization.

Compared with TFT SimpleSpell, Holy Bolt retains its main target-dispatch machinery and overrides:

| Slot | Holy Bolt VA | Observed operation |
|---|---|---|
| `+0x310` | `0x00C0CE70` | Returns order ID `0xD007C` |
| `+0x314` | `0x00C0CE80` | Returns command flags `0x140004`; full bit meanings not decoded |
| `+0x3F8` | `0x00C0C400` | Calls unit-validation helper `0x00C0D010` with Holy Bolt's classification table |
| `+0x42C` | `0x00C0C430` | Applies the unit effect using that classification table |
| `+0x434` | `0x00C0CEA0` | Returns true for an additional heal-modifier policy queried by the effect |

There are also destruction, serialization and lifecycle/notification overrides. Holy Bolt is not literally only
two virtual functions. The table above isolates the hooks relevant to the first C implementation.

The common classifier `0x00C0D2F0` uses relationship/classification predicates to index the eight-entry table at
`0x00F757A8`: `{1, -1, 1, 0, 0, -1, 0, -1}`. The callers establish `1` as the healing branch, `-1` as damage,
and `0` as invalid. The complete semantic labeling of the table's axes was not recovered.
`0x00C0D010` returns distinct validation codes, including `0x1C` when the heal target's maximum is no greater
than its current health, plus `0x96`, `0xA2` and the caller-supplied invalid-target code `0x37` on other branches.
Do not guess UI strings for the remaining numeric errors.

The damage branch in `0x00C0C430` divides its amount by the global at `0x0112D83C` before constructing the damage
operation. That global is initialized to 2 by startup routine `0x00401560`; it is zero in the file before startup.
The arithmetic path `0x009A02D0` uses reciprocal `0x009A1D00` and multiplication `0x0099FFE0`.
Thus reading the raw global as a multiplier would give the wrong conclusion. Healing has an additional modifier
branch; its complete conditions remain outside this architectural inspection.

## Consequences for Our Plan

The developer subsequently chose a Quake 2-style flags/callback design. These findings establish behavioral
responsibilities, not a requirement to reproduce the C++ hierarchy in C.

1. Use one flat ability definition with flags selecting shared processing and direct callbacks for specific behavior.
   The revised plan removes runtime `.parent` and callback inheritance; the extracted class registry remains a reference.
2. Keep common readiness and target checks before Holy Bolt's specialized check callback. That callback must not
   bypass resource, target-type or range checks. Return an enum/result with a failure reason.
3. Implement shared cast lifecycle, targeting and effect dispatch in the WC3 cast subsystem, selected by flags and
   enum state. A flag plus a single immediate `execute` call alone would not model the observed lifecycle.
4. Merge static spell-description fields into `ability_t`, while keeping mutable per-caster/per-ability state separate.
   The binary allocates instances and initializes target/timing state; it does not put that state on a singleton class.
5. Keep generic lifecycle/data/presentation code in the appropriate existing subsystem. Holy Bolt should own
   its specific validation/effect hooks. Preserve our server-authored UI and existing save/edict contracts.
6. Use independent capability/policy flags where behavior varies. There is no need for a bit per retail ancestor.

## Reproduction and Limits

The existing extractor validates known binary hashes and the registration chain:

```sh
python3 tests/test_wc3_ability_classes.py
shasum -a 256 data/warcraft3demo/game.dll 'data/Warcraft III/Warcraft III.exe'
r2 -n -a x86 -b 32 -m 0x6f000000 -e scr.color=0 -q -c 'af @ 0x6f2baac0; pdf @ 0x6f2baac0; pdg @ 0x6f2baac0; q' data/warcraft3demo/game.dll
r2 -e scr.color=0 -q -c 'af @ 0x00c0d1a0; pdf @ 0x00c0d1a0; pdg @ 0x00c0d1a0; q' 'data/Warcraft III/Warcraft III.exe'
```

The local radare2 PE loader emits missing import/type database diagnostics. For demo analysis, `-n` avoids that
loader and its file offsets equal RVAs. For TFT, the investigation used a temporary RVA image made from PE section
headers, mapped at `0x400000`; do not use `-n -m 0x400000` on the original TFT file without section remapping.
No binary was executed or modified. Disassembly/decompiler transcripts were kept under `/tmp/wc3-inheritance/`;
the addresses and interpretation here are the durable record.

Do not infer table lengths merely from consecutive executable pointers: adjacent vtables can touch. Compare
known slots and cross-check constructor writes/name getters. Decompiled x86 `thiscall` arguments are often wrong;
verify ECX setup, stack arguments and virtual-call offsets in `pdf` before assigning meaning.

See also: [registry extraction](demo-ability-classes.md) and [revised implementation plan](ability-system.md).
