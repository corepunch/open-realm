# Naga Sea Witch Tornado (ANto)

## Authored TFT contract

`ANto` is the **tornado** order; it costs 200 mana, has 120 seconds cooldown,
700 cast range, targets a selected point, `UnitID=ntor`, `BuffID=BNto`, and
both Dur/HeroDur = 40 seconds. The summon appears at the selected point (or the
nearest clear position when that point is blocked).
Its caster animation is `spell,channel`, with empty CasterArt and no EffectID.
`ntor` owns `Atdg`, `Atsp`, `Atwa`, and `Aasl` (UnitAbilities.slk).

`ntor`'s UnitUI model is `Abilities\Spells\Other\Tornado\TornadoElemental.mdx`
(inside War3x.mpq). It has Birth 2233-3333ms, Stand Walk 6867-10133ms,
and Death 13467-14500ms. There is no standalone Stand or Walk. Existing
animation-family matching accepts its combined Stand Walk for both requests.
The summon helper starts Birth using that model's sequence duration, then
regular unit movement loops Stand Walk; normal death uses the Death clip and removes the wind elemental at its end, without creating a corpse.

Other authored effects are distinct: `XNto` uses Tornado.mdx; `Btsp` has
TornadoElementalSmall.mdx (sprite,first); `Basl` has Tornado_Target.mdx.
Do not substitute these for the summoned unit or fabricate ANto CasterArt.

## Runtime behavior and owners

- `s_tornado.c`: a dedicated `CAbilityTornado` executes the channeled summon.
  The new real `ntor` is player-owned, selectable and receives normal Move
  commands; BTLF holds the 40-second timed life; the authored BNto buff is applied separately. A channel-serial/generation
  checked thinker owns damage and spin pulses and expires if the Sea Witch
  moves, dies, is stunned/silenced or cancels the channel.
- `Atdg`: 1-second building-only damage pulses. The `Area` outer radius uses
  DataA DPS, DataB/DataC the medium radius/DPS, DataD/DataE the direct
  small-radius/DPS. The bands are mutually exclusive and honor enemy targeting;
  the normal spell-damage pipeline owns combat/death side effects.
- `Atsp`: periodic ground-only enemy selection. DataB sets the spin interval;
  DataA sets the delay before the same unit can be selected again. The stock
  1.29 row uses DataA=22 seconds and DataB=3 seconds. Each lift
  applies the Cyclone `Bcyc` status for 10 seconds, independent of the
  Tornado's remaining life and Atsp's standalone Dur/HeroDur. The status owns
  its authored persistent TargetArt, raises the victim to 200 fly height, and
  restores its prior height when it expires, is dispelled, or is replaced. The
  summon thinker stores a deterministic chooser state and entity-generation-
  checked last-victim markers for 22 seconds.
- `Atwa`: uses the established autonomous-wander component, with player
  orders taking precedence. Exact Tornado-specific pursuit and leash behavior
  remains **provisional** pending retail measurement; do not make guesses in
  shared Move code.
- `Aasl`: existing Slow Aura source/recipient evaluator handles this unit's
  authored radius and slow; `Basl` target-art presentation is separate.

## Validation and open questions

The focused tests cover separate active/passive registration, the three
non-stock damage bands, enemy/ally and building/ground exclusion. The patch
must also be verified in retail and in OpenRealm (no automatic local run):

1. Cast and move the Tornado; Birth plays once, Stand Walk loops while idle
   and moving, Death once at 40 seconds. Check the original channel caster
   stays in spell,channel animation independently of the summoned model.
2. Measure 50 and 7 HP/s near/outer building bands and the intermediate band
   on the installed game-data version. Check simultaneous buildings and deaths.
3. Check enemy air/ground Slow Aura, periodic ground-only toss, hero duration,
   repeat-victim delay, spell interruption, immunity, Bcyc lift TargetArt and
   height cleanup, and Basl attachments.
4. Issue explicit Tornado Move orders, then let it idle; check the speculative
   wandering/leash policy against classic retail.
5. Cancel through Stop, movement, death and stun; ensure no leaked tornado or
   toss markers. Save/reload with a live channel and a recent toss marker.

The stock Atsp row provides the 3s spin interval and 22s same-unit delay. The
random selection order and exact Atwa destination policy remain unverified at
engine-internal fidelity. A custom map's ability rows take precedence over the
stock values.

## Shared Cyclone lift restoration

Tornado's `Atsp` writes `Bcyc` with the victim's original flight height stored
in the status `next_tick` payload. `CAbilityTornadoSpin` and ordinary
`CAbilityCyclone` both delegate removal to `S_CycloneRestoreLiftHeight`.
If `Bcyc` and `Bcy2` overlap (including map-authored variants), removal,
dispel, or expiry of either status leaves the unit at the lifted height while
the other status remains; the final removal restores the saved height. The
regression checks both removal orders via the production status dispatch.
