# Warcraft III Orc Burrows

## Runtime contract

OpenRealm models an Orc Burrow as a normal building attack plus a data-driven
cargo hold (`Abun`). The loaded Peons remain real units; cargo state controls
whether the Burrow may attack and which HUD controls are exposed.

### Empty vs occupied state

| State | Attack | Stop | Stand Down | Info panel |
|---|---|---|---|---|
| `cargo.count == 0` | hidden/disabled | hidden | hidden | normal Burrow stats + portrait |
| `cargo.count > 0` | visible/enabled | visible | visible | Burrow portrait + cargo slots |

Every gameplay attack entry point must also check `S_CargoAttacksEnabled()` so
hiding Attack is presentation, not authority.

### Cargo and attack timing

`S_CargoCapacity()` resolves the holder's `Abun`/`Acar`/`Aenc` alias and reads
its authored capacity. Standard Burrow data yields four slots, but UI slot count
must follow the ability data rather than hard-coding four.

For `N > 0` loaded Peons, the current Warsmash-parity rule is:

```text
runtime cooldown = authored cooldown / 2^N
```

If that scaled cooldown is less than or equal to the weapon damage point there
is no post-shot recovery. The attack state must begin the next swing/shot
immediately; leaving `wait == 0` in a `unit_runwait()` phase stalls after one
attack. Stop remains the normal way to terminate a persistent attack order.

### Cargo UI

When cargo is non-empty, the ordinary stat subsection is replaced by capacity-
driven cargo slots while the Burrow portrait remains visible. Each occupied
slot references the actual loaded Peon; clicking it unloads that exact unit.
Empty capacity slots keep their backdrop but have no unit icon/action.

Cargo transitions invalidate the info panel, portrait, and command card so the
first load and final unload immediately switch presentation state.

### Stand Down

`Astd` unloads all occupants through the normal safe-placement/unpause path.
Stand Down is a state command and must be visible whenever an Orc Burrow has
cargo, even when a particular Warcraft data path does not list `Astd` in the
Burrow's `UnitAbilities.abilList`. The command-card builder therefore
synthesizes the stock `Astd` button for occupied Burrows and deduplicates it if
it is also authored normally.

Individual cargo-slot unload is distinct from Stand Down. Stand Down is the
place to restore worker work policy; individual slot unload only ejects the
selected occupant.

### Battle Stations and boarding

OpenRealm implements `Abtl` as a data-driven nearby-worker call: eligible Peons
inside its area are selected up to free capacity and receive the normal board-
transport movement. Smart right-click on a compatible Burrow uses the same
boarding movement. Actual loading happens only after the Peon reaches cargo
range; it is then hidden and paused.

The bundled Warsmash checkout directly confirms generic Load/Smart boarding,
Burrow cargo combat, Stand Down, and cargo-slot UI. Its checkout does not expose
an `Abtl` implementation, so OpenRealm's nearby-worker auto-call is Warcraft
behavior layered on those confirmed cargo mechanics rather than copied from a
Warsmash `Abtl` class.

## Relevant files

- `games/warcraft-3/game/skills/s_cargo.c`
- `games/warcraft-3/game/skills/s_attack.c`
- `games/warcraft-3/game/hud/hud_unit.c`
- `games/warcraft-3/game/hud/hud_infopanel.c`
- `games/warcraft-3/game/skills/s_stop.c`
- `games/warcraft-3/game/m_unit.c`

## Shared transport unloading

`skills/s_cargo.c` shares cargo storage and removal with mobile transports.
Living boarding (`S_CargoTryLoad` and Smart's `S_CargoOrderBoard`) requires
`Acar`, `Abun`, or `Aenc`; `Amtc`/`Sch2` contributes corpse capacity only.
See [Meat Wagon corpse cargo](exhume-corpses.md).

`Adro` starts `cargo_move_unload`, a transport-owned `umove_t`: the first
passenger leaves immediately, then one leaves per Cargo Hold `Dur1` (seconds).
Zero duration is limited to one passenger per simulation frame. Repeating
Unload All while it is active does not reset the deadline or eject an extra
passenger. `Adri` stops the active order and ejects all passengers immediately;
a cargo-slot click still removes only the requested passenger.

The timer is the transport's `freetime` deadline in game milliseconds. The
existing save envelope preserves that scalar, the `F_MMOVE` active move, and
`F_EDICT` cargo references, so no extra thinker or saved C callback is needed.
`G_RunEntities -> monster_think -> cargo_move_unload.think` drives the sequence.
The normal move lifecycle cancels it on Stop, Move, death, or removal; the normal
monster scheduler suspends it while paused or stunned. On resumption an elapsed
deadline emits one passenger, without a catch-up burst.

PR #481 initially used an independent thinker, which continued unloading after
Stop/Move and during pause/stun. Its cited Warsmash `CBehaviorDrop.update` does
confirm Cargo Hold duration and first-passenger removal, but it is the unit's
active behavior, not a concurrent effect. The active move follows that ownership.
The existing point-target limitation remains: the selected point is not yet used
to move the transport or choose placement. Unloading uses the holder's position
and the shared unstuck search.

Verification: `make test-wc3-engine WC3_PATTERN='wc3_movement.unload_all*'`
covers command dispatch, duplicate orders, Stop/Move, pause/stun, death, instant
unload followed immediately by boarding, single-slot removal, mid-sequence
save/load, non-stock TFT duration, and a zero-duration ROC `Data11` hold.
`wc3_spell.meat_wagon_corpse_hold_rejects_living_unit_boarding` covers the
corpse-only gate; the existing Exhume/corpse tests cover accepted dead cargo.
