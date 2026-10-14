# Warcraft III Score Screen

This file describes the score foundation, controller, campaign-quit follow-on,
retail-presentation Patch 1, and dynamic-row Patch 2.
The installed Warcraft assets are required for visual presentation; the project
never bundles Blizzard FDF or score artwork.

For the repeatable retail workflow used to capture score traces, edit disposable
map copies, repack them safely, and compare UnitBalance variants, see
[Retail Score Experiments](retail-score-experiments.md).

## Source and confidence

The raw score counters are simulation-authored as described in the preceding
foundation patch. Four **unverified guesses** now return values through
`GetPlayerScore` and are also shown on the result screen. They are deliberately
separated from the accounting hooks in `g_score.c` and tagged `GUESS`:

| Player score | Temporary algorithm (NOT confirmed retail) |
|---|---|
| Tech percent | 100 × researched technology IDs / observed technology IDs, or 0 with none |
| Units total | 10× trained + 10× enemy units killed + 20× structures built + 20× structures razed |
| Heroes total | 100× hero kills + 10× items gained + 10× experience gained |
| Resources total | floor(gross gold / 10) + floor(gross lumber / 5) |
| Overall total | **Confirmed:** Units + Heroes + Resources (no Tech bonus) |

All totals saturate at `INT32_MAX`. **These weights and denominators are guesses,
not reverse-engineered Warcraft calculations**. In particular, the technology
denominator lacks unresearched-but-available tech; hero XP is now tracked but
its 10× score weight is inferred from one retail case; and resources do not
have authoritative transfer/tax accounting. Compare
against original retail scores with controlled games before claiming parity.

## Snapshot and screen lifecycle

The first `EndGame(true)`, `ChangeLevel(map, true)` or `RestartGame(true)`
queues a transient deferred destination and captures a complete immutable player
score snapshot. The first request wins; duplicate exits cannot overwrite it.
A pending `SaveGame`/`ChangeLevel` pair **commits the zone save first** and
queues the outgoing score screen only if that save succeeded. Failed saves do
not load or show the score modal. Values and names survive unloading because
the frozen snapshot is not tied to live edicts; the snapshot and selected tabs
are NOT written to zone saves.

At the end of the frame, the Warcraft game module loads the retail
`UI\FrameDef\Glue\ScoreScreen.fdf` and binds its `ScoreScreenFrame` root.
It authors a `svc_window` with a unique score ID, modal input and the standard
client-owned pause semantics. The same window also carries generic
`UI_WINDOW_PRESENTATION`, which makes the client draw only the presentation
window over a cleared screen instead of the 3-D world, HUD or other windows.
This is an opt-in generic client capability, not a WC3-specific client screen.
It remains active across tab refreshes and ends when the score window closes. Four page buttons switch between Overview, Units,
Heroes and Resources, re-emitting the SAME unique window; a Continue click
sends `close_window_command scorescreen_continue`. The command is accepted
only from a recipient who was actually shown the window, and it consumes the
deferred destination *before* submitting the menu or map change.

Only included score-screen players appear. Each explicit `SetPlayerOnScoreScreen`
override is honoured independently; non-overridden players use map-controller
eligibility instead of including every used/connected slot.
Removed/eliminated players remain visible when eligible.

If the FDF root is absent, the UI cannot be sent, or there are no connected
recipients, the engine immediately follows the previously resolved destination.
A broken/absent MPQ must not strand the outgoing session.

### Retail FDF and artwork: presentation Patch 1

The RoC and TFT `UI\FrameDef\Glue\ScoreScreen.fdf` files are reportedly
byte-identical (based on the game-data research report of 2026-10-13). The
repo continues to use **mounted retail assets**, and does not bundle Blizzard
FDF, model or image files. The manually generated-style binding contract in
`generated/score_screen.h` covers the FDF root/main panel, background, page
tabs, top bar, race artwork, four- and five-column header banks, tab controls,
selected-tab text and OK button. The exact FDF sources were **not uploaded**;
regenerate/check this binding with `fdfbindgen` using the extracted FDF when it
becomes available.

The four authored `MODEL` layers are presented through the generic 2-D
`FT_SPRITE` path rather than a portrait camera:

| FDF frame | Retail asset and selection |
|---|---|
| `ScoreScreenBackground` | Authored ScoreScreen-Background MDX; `Scorescreen-4Button` or `Scorescreen-5Button` by page |
| `ScoreScreenPageTab` | Authored ScoreScreen-Tabs MDX; sequence `Tab1`...`Tab4` |
| `ScoreScreenBarTop` | Authored ScoreScreen-TopBar MDX; `TeamColorNN Stand` |
| `ScoreScreenRaceArt` | Skin-resolved race/edition victory model or shared ScoreScreen-Defeat MDX |

The real FDF defines tab/header/button positions and fonts. The score screen
retains its FDF `SetAllPoints` root instead of being re-centered as a gameplay
window. The original tab controls dispatch the existing `scorescreen_tab`
command and the original OK control dispatches `scorescreen_continue`. The
header text uses `GlobalStrings.fdf` keys rather than hard-coded headings.

### Score tables: dynamic-row Patch 2

The retail FDF creates the background, header banks, tabs and scrollbar but **not**
player score rows. Patch 2 constructs a bounded viewport of ten runtime rows.
Every row owns a translucent player-colour strip, stock silver banner art,
original `scorescreen-player-{race}.blp` icon where supported, name, and up to
five separately positioned numeric cells. Numeric-cell centers reuse the
**exact FDF header offsets** for 4-/5-column pages, while row start,
height/spacing, text size, tint and icon placement remain **PRESENTATION
GUESSES** pending retail runtime verification. The player ordering is the
frozen score snapshot order; retail's exact order/grouping rules are unknown.

The initial Patch 2 pages followed the keys from `GlobalStrings.fdf`; see the corrective section below for expanded columns:

| Page | Displayed columns |
|---|---|
| Overview | Unit Score, Heroes Score, Resource Score, Total Score |
| Units | Units Produced, Units Killed, Buildings Produced, Buildings Razed, Largest Army |
| Heroes | Heroes Used, Heroes Killed, Items Obtained, Mercenaries Hired, Experience Gained |
| Resources | Gold Mined, Lumber Harvested, Resources Traded, Tech Percentage, Gold Lost to Upkeep |

Raw counters from the frozen snapshot populate known columns. The initial
patch showed `-` for untracked Heroes Used, Experience Gained and Resources
Traded; these now have tracking/provisional projections as described below.
Technology percentage and component aggregate weights retain explicit `GUESS`
comments in `g_score.c`; the overall sum is supported by the retail reference. Highlighting the
highest positive score per column is an additional **presentation guess**
inferred from the screenshot, not verified retail sorting logic.

Retail's Heroes Used cell is a list of hero portraits with each hero's current
level, not an aggregate count. Controlled retail maps show that it follows
current ownership, displays duplicate instances separately, retains dead or
hidden heroes, and omits removed heroes. See
[Heroes Used presentation experiments](retail-score-experiments.md#heroes-used-presentation-experiments).
OpenRealm's scalar hero-entry count is diagnostic data and must not be
presented as the retail cell; implementation parity requires snapshotting
current player-owned hero instances and rendering their portraits and levels.
Static preplaced heroes and campaign-cache restoration seeded by prior mission
progression remain unverified.

The snapshot now captures player colour and gameplay elapsed time using
`level.framenum * FRAMETIME`, capped at 32-bit milliseconds. This reflects
simulation time, not pause time spent reading scores. The existing elapsed
label is FDF-authored; the controller binds its value if available, otherwise
positions a runtime text value near the authored label. This fallback needs
confirmation against the extracted FDF source.

The FDF scrollbar is reused for multi-player overflow. The generic window
client forwards an opt-in standalone scrollbar's normalized user scroll
position through its authored command, and the game validates the requesting
player and frozen result lifecycle before changing which rows are presented.
No out-of-range player row is serialized. A short table hides the scrollbar.
Scroll position is per recipient and is retained across score tab changes.
The scrollbar accepts direct wheel/thumb input; exact drag/track behaviour
remains subject to local visual/manual verification.

Missing mandatory FDF frames prevent score-window presentation so deferred
navigation can proceed; optional cosmetic/model data is not replaced with
OpenRealm art. No local compilation or runtime validation was performed.

### Score interactions: Patch 3

The original four- and five-column **FDF checkbox buttons** now dispatch
`scorescreen_sort <column>` intents. Sorting is **per recipient** over a temporary
index array into the immutable match snapshot; it never mutates source scores.
Clicking a column first sorts descending, clicking it again ascending. Missing
statistics always follow real scores and ties retain prior snapshot order.
Changing tabs returns to the snapshot order (sort selection clears) but retains
the viewer's scroll offset. Header checked states display the active column;
blank Units headers are hidden rather than clickable.

The retail `ScoreScreenTeamsButton` checkbox groups players by numeric team ID
when enabled. **GUESS:** sorting within each team uses the selected column,
while the initial order uses the outgoing snapshot. This is provisional: exact
retail grouping, team totals and tie rules require retail runtime evidence.
The optional ally-colour FDF control uses the candidate name
`ScoreScreenTeamColorButton`; the agent's report did not give its exact name,
so if absent no replacement visual control is invented. When present it toggles
the player-strip colour to the lowest-numbered teammate's player colour.
**GUESS:** this is only a substitute for retail's local ally-colour rules, not
an assertion of parity. Race icons remain the player's actual race.

The generic FDF serialization bridge now preserves `ControlShortcutKey` as a
window button hotkey. The retail score-screen OK button has shortcut **O**,
which follows the same recipient-validated `scorescreen_continue` transition
as mouse click. Escape remains blocked; no second transition is sent when a
stale viewer command arrives after completing the score screen. The optional
Save Replay / Replay Saved controls are hidden until the project has a real
replay-save backend, avoiding inert or misleading UI controls. All new user
commands validate the waiting score-viewer slot and selected page before
altering the view.

All rules above that are explicitly marked GUESS remain open for controlled
retail screenshot/run comparison. The original FDF source and full asset set
were not provided with the research report, so optional control identifiers
still need confirmation against the extraction.

## Manual validation needed

- Launch a finished single-player mission with `EndGame(true)` and inspect
  model selection, real FDF tabs/headers, font sizes and OK routing.
- Compare a defeat screenshot at 4:3 against the provided retail screenshot:
  check parchment and defeat artwork, absent world/console, column bank,
  tab animation, OK button, black sidebars on widescreen, row icon/tint/geometry,
  aligned scores, elapsed time and horizontal/vertical clipping.
- Verify no renderer or presentation flicker when switching tabs, closing the
  exclusive score window, or opening with `r_hud 0`.
- SaveAndChangeLevelBJ: confirm that the script save commits *before* the score
  modal opens and Continue loads the intended next map once.
- `ChangeLevel(..., false)` still moves directly, never opening the modal.
- Multiplayer: every connected participant sees frozen columns; a Continue
  from any displayed recipient releases the shared server destination.
- Compare guessed aggregate values against retail under controlled matches.
- With more than ten eligible players, wheel and drag the scrollbar; verify
  the server displays precisely the expected page without stale hidden rows.
- Click each page's populated column headers twice and verify descending then
  ascending score order, stable ties, missing values last, and per-player views.
- Toggle Teams and the ally-colour control (if the FDF binds it), and compare
  the provisional grouping/palette against controlled retail teams.
- Press O on the score screen; it should trigger the same OK action once.
- Verify Heroes Used, Experience and Traded show a dash until tracked, and
  that resource column values follow the retail headings rather than arbitrary
  fallback score totals.
- Ensure a missing `ScoreScreen.fdf` proceeds to the destination.

Compilation, gameplay, rendering and the above observations remain for the
local build/test run. `AGENTS.md`, `CONTRIBUTING.md` and `docs/ui-authoring.md`
remain authoritative.

### Retail FDF parser and campaign Quit Mission fix (2026-10-13)

The stock `ScoreScreen.fdf` uses `LayerStyle "NOSHADING"` and related layer
styles. The shared FDF parser now consumes and retains `NOSHADING`,
`IGNORETRACKEVENTS` and `SETSVIEWPORT` flags; ignored tracking is propagated
to frame hit-testing. Unlit rendering and viewport scissor enforcement remain
separate renderer concerns (the style bits are retained, not silently dropped).
Malformed FDFs no longer enter the successfully-loaded FDF cache.

The campaign Quit Mission command now queues the same frozen score snapshot
and score-screen transition as `EndGame(true)`, whereas Exit Program and
non-campaign quit retain their existing routes. If the retail score FDF cannot
be parsed or the player cannot be shown the modal, the existing fail-open
transition returns to campaign selection instead of stranding the session.

Validation: exercise both the scripted Defeat -> Quit Campaign dialog and
ESC -> End Game -> Quit Mission on retail MPQ data. The `LayerStyle` parsing
regression is supplied but local compilation/runtime testing was not performed.

### Retail score-data corrections (13 October 2026)

The UndeadX07c reference captures at ~9 seconds show two participants
(Naphos and Faceless Ones), 7 and 50 Units Produced, 3 Heroes Used and
500 Experience Gained for Naphos, and **Total Score = Unit + Hero + Resource**.
The reference is *evidence for this map*, not a proof of general retail weights.
The prior fallback enumerated script-only map factions and returned 550/250
aggregate points from an unrelated Tech Percentage bonus.

The score snapshot now honors each explicit `SetPlayerOnScoreScreen(false)`
independently; when unset, only W3I human/computer participants appear by
default (plus connected synthetic players without W3I data). It keeps defeated
participants, and uses the local `name` profile cvar for single-player humans
unless subsequent JASS `SetPlayerName` overrides it. Nonstandard scripted
controller changes and retail eligibility tie-breaking remain to be verified.

`Units Produced` now includes preplaced W3I units, successful immediate
`CreateUnit` units, and completed training; it deliberately does **not** count
queued trainees at reservation, or transformations as new units. The preplaced
and runtime paths do not double-count script-restored map units because the
existing `unit_createorfind` resolution reuses entities. Buildings Produced
still measures finished construction only. Largest Army provisionally uses
peak food used, already tracked by the authoritative food subsystem. All
W3I participants now get an initial preplaced-food pass, not only clients that
connect, so AI armies contribute to their own largest-army peaks.

The current scalar Heroes Used counter counts units entering play that are
classified as heroes; it is diagnostic only and does not match retail's
portrait-and-level presentation. Experience Gained is the sum of positive hero
XP changes through the common
`G_HeroSetXP` path, including JASS grants and combat XP. The Heroes page renders
both values; the Heroes Score formula now includes **GUESS** 10× XP after the
single retail sample (500 XP => 5000 Hero Score). The pre-existing kill and
item weights are **GUESS** too. The temporary Unit Score is still weighted by
raw event counts rather than Warcraft unit-cost values; do not expect it to
match the retail 12465/2325 yet.

The Units page now exposes all five retail fields: produced, killed, buildings
produced, buildings razed and largest army. Resources Traded shows the retail
`gold/lumber` presentation, currently using the authored give counters as a
**GUESS** because bidirectional trade semantics have not been recovered. The
Tech Percentage denominator is **still unverified**; the UI displays its value
without `%`, matching the sample reference's typography. Victory/defeat art
selection and the 3-D MDX background are intentionally left to the separate
presentation work.

Save version **91** captures the new hero totals and explicit inclusion marker,
rejecting prior save versions according to the project's save policy. This
patch does not change the 25 JASS PLAYER_SCORE_* numeric identifiers.

### Scripted hero level score separation (2026-10-13 follow-on)

`SetHeroLevel` and `SetHeroLevelBJ` use the common hero XP/level transition
without crediting the XP implicitly required by an authored level increase.
Direct `SetHeroXP`, `AddHeroXP`, and combat grants still credit positive XP.
This is a deliberate interpretation of **Experience Gained** as awarded XP
rather than a hero's cumulative XP threshold; verify against retail maps.
It does not yet resolve the retail Unit Score formula, Tech Percentage, or
Largest Army (preplaced AI food accounting).

### Cost-weighted scoring and diagnostics (follow-on)

Produced/killed units and built/razed structures now accumulate their authored
`UnitBalance` gold-plus-lumber costs independently of the raw counts. This is
**provisional** for retail: exact contribution rules for heroes, kills and
zero-cost/custom campaign units require controlled comparison. The new persisted
value totals require save version 92. When `wc3_game_result_debug 1` is set,
unit creation, food deltas/peaks, hero creation and XP changes, and provisional
tech numerators/denominators are logged. These events can be verbose on maps
with large armies. No mission-specific correction is applied to Largest Army,
Heroes Used, XP or Tech Percentage without verified causality.

### Score startup and scripted hero XP attribution (follow-on)

The map-setup food peak is provisional until the first connected client begins
play; on frame zero, each map participant's score-only Largest Army baseline
is reset to its current authoritative food usage. This leaves gameplay food
accounting unchanged, and subsequent food peaks still accumulate normally.
Scripted `SetHeroLevel` XP is retained on the hero entity (including across
ownership transfers) and is added to the outgoing score for its **current**
owner. Ordinary earned XP remains event-attributed. This approximates the
UndeadX07c retail 500 versus the temporary neutral hero's 4700 without map
name/hero ID special cases; other campaign hero restoration rules need checks.
The new entity field requires save version 93. Unit cost values and technology
percentage remain provisional pending further retail evidence.

### Post-main Largest Army baseline and unit audit

The initial score-only Largest Army peak is captured after the first playable
simulation pass, following JASS initialization and deferred unit removals.
`ClientBegin` may run on a nonzero frame and is not a reliable startup
checkpoint. With `wc3_game_result_debug 1`, the `score baseline survivor`
entries enumerate live unit rawcodes/food/cost and the `score food gameplay
baseline` summary compares the previous peak, authoritative current food,
surviving unit-food total and accrued unit score. This deliberately does not
change `Units Produced` or unit score: the retail inclusion rule for scripted
preplaced armies remains unverified. All subsequent food peaks still update.

### Hero lifecycle and technology classification audit

`wc3_game_result_debug 1` reports hero creation, ownership transfers and
game-cache restoration with identity, level and XP. Retail experiments
establish that the display follows current ownership and lists hero instances
rather than historical ownership counts. The scalar creation counter is not a
retail presentation value. Prior-mission cache restoration and static
preplaced-hero behavior remain open. Tech Percentage now uses
researched *levels* of authored `UpgradeData` rows seen in the player's tech
state, instead of treating arbitrary tech-limit rows as researched upgrades.
The diagnostic retains the previous observed-entry percentage alongside the
classified numerator and denominator. **The authored race-wide denominator is
not yet recovered**; therefore the classified result remains provisional and
should be compared against retail in multiple missions before declaring parity.

### Race-wide technology percentage candidate

Technology percentage now enumerates all authored `UpgradeData.slk` rows with
`used=1`, positive `maxlevel`, and a race matching the player's race, rather
than limiting the denominator to technology entries explicitly touched at
runtime. Each eligible upgrade contributes its maximum level count; its
completed researched levels (clamped to the maximum) contribute to the
numerator. `SetPlayerTechMaxAllowed` does not count as completed research.
The computation uses integer truncation and returns zero when no eligible
levels exist. With `wc3_game_result_debug 1`, `score tech eligible` reports
each included upgrade, and `score tech player` summarizes both the previous
observed-entry metric and the race-wide result. This reproduces the observed
UndeadX07c 32%/0% comparison but remains a candidate retail rule pending
verification in other races, expansions and custom maps.

### Unit/food lifecycle diagnostics (score research)

`wc3_game_result_debug 1` additionally reports `score lifecycle` events for
map-unit entry, immediate JASS creation, training completion, scripted
`RemoveUnit` requests, end-of-tick retirement and eventual edict freeing, unit death, ownership transfers,
and training food reservation. Each entry carries the entity number, rawcode,
owner, stock-resolved gold/lumber costs, authored and active food usage,
player's current/peak food, and accumulated produced unit count/value. `score
food state` shows before/after food-used and cap transitions, with the score
peak on both sides. `score death attribution` and `score death credit` distinguish
scripted `KillUnit`, combat kills, and deaths credited to another player.
`jass_remove_requested` is separate from `free_begin` because removal may be
deferred to the end of the simulation tick. These diagnostics are **read-only**;
score totals and gameplay food are unchanged. They are investigative and should
be removed once the retail inclusion/lifecycle rules are determined.

For UndeadX07c, compare the 10 deterministic Normal/Easy script removals
against `score_unit_credited`, `jass_remove_requested`, and `free_begin`
records. Do not infer retail behaviour from the OpenRealm logs alone;
retail experiments must establish whether particular removed units keep score
credit and when Largest Army tracking begins.

### ShowUnit visibility transitions

With `wc3_game_result_debug 1`, `ShowUnit` emits `score lifecycle`
`showunit_hide_before/after` and `showunit_show_before/after` records, including
the hidden bit, unit/owner identity, current food, score food peak, and unit
score. `showunit_unchanged` and `showunit_rejected_inactive` identify ignored calls.

### Retail Largest Army startup peak correction (2026-10-13)

Retail TFT 1.29.2 Normal-difficulty `UndeadX07c` tracing reports a peak
food usage of 171 before scripted unit removals. Current food then falls
to 127, but `Largest Army` remains 171. Keeping the removed units yields
171 current and peak food without changing the 50 Units Produced or 12,465
Unit Score. Consequently, the first-playable-frame survivor audit must not
reset `WC3_PLAYER_SCORE_FOOD_MAXUSED` to current food. The audit remains
read-only; the food subsystem continues accumulating new peaks.

Retail Hard screenshot reporting 71 has not been reproduced by this Normal
experiment. The separate unit-cost score discrepancy (12,465 versus OpenRealm
15,030) remains unresolved and is not adjusted. The apparent Heroes Used
discrepancy was a comparison error: retail's `3` is Arthas's level label, while
OpenRealm's `2` was a scalar hero creation count; the retail portrait-list
presentation is not yet implemented.

### Score diagnostic cleanup (2026-10-14)

Removed investigation-only unit lifecycle tracing and first-frame survivor
audits. Largest Army remains the highest observed food usage, including
scripted setup peaks, without a post-startup reset or scan. Optional bounded
score summaries are compiled via `WC3_DEBUG_SCORE=1` (internal
`BZ_WC3_DEBUG_SCORE`) and gated by `wc3_game_result_debug 1`; gameplay and score
accounting remain independent.
