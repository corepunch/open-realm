# Map startup seed and player races

Payoff171 closes NUM-04.5 for the recovered local test-map startup producer.
Production map loading now gives the path owner its boot state, evaluates map
configuration, stores one host setup stamp, selects the game seed, resolves
12 logical player slots, and enters map `main()`. Movement consumes the stored
generator state; it never samples wall time.

Retail 1.27 `-loadfile` reads WorldEdit preference `Test Map - Fixed Random Seed`
(index `0x6a`, missing value defaults to enabled) at `6f2e2850`. Its descriptor
adds `MAP_LOCK_RANDOM_SEED` (`0x8000`). `6f2a46a0` stamps the lobby setup record
with `GetTickCount`; `6f96c8c0` deserializes record seed at offset `0x0c`.
`6f29e300` selects `0x77617233` when locked, otherwise that record word.
The separately observed rotate/fold code is a no-record branch and did not run
in these local startup captures. The previous description of the unlocked
seed as a computed lobby fold was incorrect.

The engine's local preference is `wc3_lock_random_seed` (default `1`); `0`
disables that preference without clearing an authored map flag. A seed supplied
through `wc3_random_seed` replaces the host stamp, including explicit seed `0`.
Otherwise the engine stores one `gi.Milliseconds()` result in
`level.setup.random_seed`. Host-clock origins differ across platforms; identical
unlocked sessions require the same stored setup word. Simulation results never
depend on subsequent host timing. These settings belong to the WC3 game module.
The engine import supplies a generic host clock, shared by the normal and
headless executables.

Before configuration, the owner receives `0x69707365` (`6f157610`), producing
`1768977253 / 2822785048`. After the game seed, `6f1e9dd0` resolves all 12 logical
player numbers. A Random preference consumes exactly one word and chooses
`(word >> 30) + 1`; fixed preferences consume none. Client storage order can
change when the local map player occupies another slot, so it cannot determine
draw recipients.

A related native bug is fixed: `SetPlayerRacePreference` (`6f213f30`) writes
`(old & 0x40) | (requested & ~0x40)`. It replaces all other bits. OR accumulation
left stale Random bits after a fixed choice and changed startup RNG consumption.
The saved Ghidra method has cdecl player/preference arguments at stack offsets
4/8 and a plain `RET`. Native regressions also preserve unknown requested bits
and refuse to introduce bit `0x40` from the requested value.

| Profile | Race draws | Owner after map initialization |
|---|---:|---|
| Locked, four fixed and eight unset slots | 8 | `4273436052 / 209508436` |
| Locked, mixed fixed/random slots | 4 | `3946970562 / 277349488` |
| Locked, all fixed | 0 | `3292812501 / 548955360` |
| Unlocked mixed, record `82172056` | 4 | `4160792303 / 1486880788` |
| Unlocked mixed, record `82215974` | 4 | `2707349937 / 2824099852` |

The mixed/fixed profiles execute two public integer queries and one real query
in `main()`. Tests compare their stored real words directly, avoiding a second
conversion through the independent retail decimal-literal parser. Three small
fixture MPQs reproduce these rosters through `globals.LoadMap`, ordinary
`CreateUnit` and public Move. Loader observations confirm boot state; first
movement and cold save/load preserve each initialization state. Save format138
retains the setup seed and flags together with the current path-owner state;
loading does not restamp the session or rerun `main()`.

The strict verifier reconstructs all owner and purpose-stream observations in
10 archived captures: 1611 owner draws, 530 stream draws, nine completed captures
and one explicitly incomplete loading run. Five distinct locked/unlocked
profiles feed the native test header. Four complete comparisons connect two
locked maps to observer-free controls. Attempts whose registry key was wrong
are retained as locked repeats. The unlocked observer-free attempt was
incomplete and is not claimed as parity evidence.

```sh
python3 tools/ghidra/research/verify_startup171_seed.py \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27/research \
  --output /tmp/startup171.json
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 \
  make test-wc3-engine WC3_PATTERN='wc3_map_random.*'
```

The shared RNG arithmetic and lookup words already have a separate original-code
oracle. Startup verification checks raw capture/preload hashes, all modeled
states, milestones, draw callers, host-stamp/record equality, completed movement,
and regenerated header equality. Negative tests reject missing completion,
observer failure, incorrect boot state, and lock/stamp disagreement.

Purpose-stream ownership and consumer migration remain NUM-04.6: this change
does not merge them into the path owner or claim to have replaced legacy
`srand`. Observer payload78 and network/replay transport are assembly-only;
unused engine slots are not treated as equivalent to retail observers. The
retail handoff is [NUM-04.5](retail-pathfinding-handoffs/NUM-04.5/HANDOFF.md).
