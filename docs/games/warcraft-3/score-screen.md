# Warcraft III Score Screen

OpenRealm's score-screen work is split deliberately between authoritative Warcraft III simulation state and later UI/session presentation. The generic client must not calculate Warcraft scores or own result policy; the Warcraft III game module records score inputs and will author the eventual screen through the normal `svc_window` path.

## Current foundation

`common.j` exposes 25 `playerscore` ids through `GetPlayerScore`. OpenRealm now keeps a 32-bit per-player score state using that exact numeric order and records the high-confidence raw categories at their authoritative gameplay completion points:

- completed trained units;
- credited unit kills, hero kills, and structures razed;
- completed structures;
- maximum food produced and maximum food used;
- successfully acquired items;
- successfully hired neutral-shop units;
- gross gold/lumber gathered;
- gold credited after upkeep and gold lost to upkeep;
- lumber lost to upkeep when a map changes the lumber upkeep rate.

Resource income also updates Warcraft's cumulative `PLAYER_STATE_GOLD_GATHERED` and `PLAYER_STATE_LUMBER_GATHERED` states. Conversely, an explicit `SetPlayerState` write to either gathered state updates the matching raw score source, so map-authored state and score queries do not diverge.

The accounting hooks live in the owning simulation paths rather than trigger callbacks. Training counts only after the trained entity is exposed as complete; construction counts in `G_CompleteConstruction`; kill credit is recorded in the central unit-death path; resource totals are recorded where upkeep is applied; item acquisition records only a successful inventory transfer; and mercenary hiring records only a successful neutral-unit sale. Cancelled or failed actions therefore do not need compensating decrements.

## `GetPlayerScore`

`GetPlayerScore` is registered and returns the raw values whose semantics are represented directly by current engine state. The following ids remain intentionally unsupported rather than being populated with guessed retail formulas:

- `PLAYER_SCORE_TECH_PERCENT`;
- `PLAYER_SCORE_UNIT_TOTAL`;
- `PLAYER_SCORE_HERO_TOTAL`;
- `PLAYER_SCORE_RESOURCE_TOTAL`;
- `PLAYER_SCORE_TOTAL`.

Requests for one of those values produce a JASS runtime error. This is deliberate: the aggregate weighting and exact retail technology denominator have not yet been established from authoritative data or controlled retail measurements. A plausible formula would be worse than an explicit missing capability because custom maps can query these values directly.

The currently-zero raw transfer/tax categories (`*_GIVEN`, `*_RECEIVED`, `*_LOST_TAX`) remain valid storage slots, but they will stay zero until the corresponding authoritative Warcraft resource-transfer/tax transaction path exists. Score code must be added at that transaction boundary rather than inferred from resource-balance deltas.

## Save/load

The score counters are simulation state and therefore live inline in `gameClient_t`. A fresh map resets them in `G_InitMapPlayer`; restoring a save reloads its recorded totals after map initialization. Save format version 90 persists them with the client record. OpenRealm uses exact-version save compatibility, so version 89 and earlier are rejected rather than migrated.

Transient future score-screen presentation state must not be serialized merely because the counters are. In particular, the open score window, selected tab, frozen presentation snapshot, and pending client modal ownership are presentation/session state unless a later retail-parity investigation proves otherwise.

## Score-screen inclusion

`SetPlayerOnScoreScreen` remains separate from the counters. Its `on_score_screen` flag controls whether a player is eligible to appear when the future screen snapshot is built; it does not mean connected, playing, winner, loser, or observer. Before the UI starts filtering on this flag, the default/Blizzard.j initialization path must be verified so an ordinary game cannot accidentally produce an empty screen.

## Retail presentation target

Warcraft's authored glue frame is `UI\FrameDef\Glue\ScoreScreen.fdf`, with `ScoreScreenFrame` as the main control and score-screen tab/column/button templates supplied by the same data. In accordance with the UI authoring rules, OpenRealm should parse that retail FDF through the existing FDF pipeline instead of committing a replacement project FDF or hardcoding substitute geometry.

The visible score screen has four conceptual pages:

- Overview;
- Units;
- Heroes;
- Resources.

The public `playerscore` native set is not sufficient by itself to reconstruct every visible row (for example the retail Heroes page includes values such as heroes used and experience gained). Those extra presentation statistics should be added to a frozen game-side snapshot only after their exact semantics and source events are established; they should not be invented as new JASS score ids.

## Remaining lifecycle work

The actual score-screen controller is still missing. `EndGame(doScoreScreen)`, `ChangeLevel(map, doScoreScreen)`, and `RestartGame(doScoreScreen)` continue to use the existing safe deferred session boundary and currently consume but do not display the score screen. Do not interpose a half-built screen that can strand those transitions.

The next presentation patch should:

1. identify/bind the real retail score-screen FDF controls;
2. freeze an outgoing-match score snapshot before any map/frontend transition destroys the game state;
3. extend the existing deferred transition representation with the already-requested destination plus `show_scores`;
4. author the screen from the Warcraft III game module through an ordinary modal, unique `svc_window`;
5. let the existing modal ownership pause the simulation rather than adding a score-specific pause path;
6. use the established `close_window_command <server command>` pattern for the Continue/OK action;
7. execute the already-resolved deferred destination exactly once after the score window closes.

No Warcraft-specific score-screen parser, renderer, command, or controller belongs in the generic client.

## Regression coverage

The in-engine tests cover the accounting at existing production boundaries: food maxima and upkeep resource income, trained-unit completion, construction completion without double counting, item acquisition, neutral-unit hiring, death-category credit, JASS `GetPlayerScore`, `SetPlayerState` gathered-state synchronization, persistence, and rejection of earlier save versions, including version 89.

Visual parity remains a manual/future integration concern because the test archive intentionally does not contain Blizzard retail assets. When score-screen FDF binding is implemented, deterministic tests should provide only the minimal fixture resources at the same archive paths used by retail data and assert the emitted generic window/frame contract rather than relying on a developer's local Warcraft installation.
