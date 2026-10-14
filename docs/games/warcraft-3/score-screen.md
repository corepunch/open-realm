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

### Custom map score-screen artwork overrides

The score screen uses the stock `ScoreScreen.fdf` model path, not a W3I
background field. The renderer resolves a map-scoped MDX first and falls back
to the installed game model when no imported replacement exists. The MDX's
file-backed texture references use the same map-first lookup; common parchment
texture overrides include `scorescreen-background-L1.blp`, `-L2.blp`, `-R1.blp`,
`-R2.blp`, and `-sort4.blp` under
`UI\Glues\ScoreScreen\ScoreScreen-Background\`.

The renderer's candidate-order regression covers these five paths, alternative
map scopes, stock fallback, and the corresponding background MDX lookup.
It **does not** substitute for running a real custom `.w3x` with imported BLPs
and checking the GL output: the test fixture does not ship that proprietary
artwork. The manual comparison should end a match, inspect both four- and
five-column pages, leave to a different map without imports, and confirm the
original textures and model return. Keep the outgoing map scope active until
Continue; do not unmount/reset it merely because the world is not rendered.

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
- Compare each Heroes Used portrait/level pair with retail after transfers,
  hiding, death, revival and removal. Verify the frozen snapshot survives tab
  changes and that resources follow retail headings rather than guessed totals.
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
(Naphos and Faceless Ones), 7 and 50 Units Produced, one level-3 Arthas
portrait in Heroes Used, and 500 Experience Gained for Naphos. The `3` in that
portrait is a hero **level**, not a count. **Total Score = Unit + Hero + Resource**.
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

The current Heroes Used presentation snapshots current player-owned hero
instances and displays their portraits with level labels. Experience Gained is
the sum of positive hero XP changes through the common
`G_HeroSetXP` path, including JASS grants and combat XP. The Heroes page renders
the hero roster and XP value; the Heroes Score formula now includes **GUESS**
10× XP after the single retail sample (500 XP => 5000 Hero Score). The
pre-existing kill and item weights are **GUESS** too. The temporary Unit Score
is still weighted by raw event counts rather than Warcraft unit-cost values;
do not expect it to match the retail 12465/2325 yet.

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
At that stage, Unit Score, Tech Percentage, Largest Army, and Heroes Used
still required retail comparison; see the later corrections below.

### Cost-weighted scoring and diagnostics (follow-on)

Produced/killed units and built/razed structures now accumulate their authored
`UnitBalance` gold-plus-lumber costs independently of the raw counts. This is
**provisional** for retail: exact contribution rules for heroes, kills and
zero-cost/custom campaign units require controlled comparison. The new persisted
value totals require save version 92. Earlier instrumentation traced every
unit and food change; that investigation-only output has since been removed.
The opt-in diagnostic build retains bounded score/result summaries instead.
No mission-specific correction is applied to Largest Army, Heroes Used, XP
or Tech Percentage without verified causality.

### Score startup and scripted hero XP attribution (follow-on)

An earlier implementation reset the map-setup food peak after the first
playable frame. Retail comparison proved that reset incorrect; it and the
survivor audit have since been removed. Subsequent food peaks still accumulate
normally.
Scripted `SetHeroLevel` XP is retained on the hero entity (including across
ownership transfers) and is added to the outgoing score for its **current**
owner. Ordinary earned XP remains event-attributed. This approximates the
UndeadX07c retail 500 versus the temporary neutral hero's 4700 without map
name/hero ID special cases; other campaign hero restoration rules need checks.
The new entity field requires save version 93. Unit cost values and technology
percentage remain provisional pending further retail evidence.

Retail testing of HumanX03 exposed a stock XP-threshold bug in the formula
extension: OpenRealm used 1000 XP for the scripted level-4 Kael, plus 500 for
level-3 Vashj, producing 15000 Heroes Score. Retail reports 900 + 500 XP and
14000 Heroes Score. `NeedHeroXP=200` with the stock A/B/C formula was being
extended using the absolute table index, creating a 500-XP third requirement
instead of 400. The stock progression now uses the measured `200,300,400,...`
sequence. Regression coverage is in `wc3_combat.stock_misc_hero_xp_formula_matches_retail_thresholds`
and `wc3_score.stock_level_four_and_three_scripted_xp_total_retail_value`.

### Largest Army startup, hero and technology investigations

The first-playable-frame survivor enumeration was temporary instrumentation
and has been removed. Largest Army retains the peak food usage observed during
map initialization even when JASS subsequently removes units; there is no
post-main reset or diagnostic full-world scan in normal gameplay.

Hero creation, ownership changes, game-cache restoration and ShowUnit
visibility were previously traced during the Heroes Used investigation. Those
verbose per-unit traces have been removed; ownership and score calculation
remain independent of the diagnostic switch. Retail evidence establishes
current-owner portrait presentation; prior-mission cache restoration and
static preplaced-hero behavior remain open.

The technology-percentage candidate counts researched levels against
race-matching authored `UpgradeData.slk` maximum levels. The obsolete
observed-entry calculation and per-upgrade logs have been removed. The
candidate rule remains unverified beyond the current retail comparisons.

### Retail Largest Army startup peak correction (2026-10-13)

Retail TFT 1.29.2 Normal-difficulty `UndeadX07c` tracing reports a peak
food usage of 171 before scripted unit removals. Current food then falls
to 127, but `Largest Army` remains 171. Keeping the removed units yields
171 current and peak food without changing the 50 Units Produced or 12,465
Unit Score. Consequently, the first-playable-frame survivor audit must not
reset `WC3_PLAYER_SCORE_FOOD_MAXUSED` to current food. The audit remains
read-only; the food subsystem continues accumulating new peaks.

Retail Hard screenshot reporting 71 has not been reproduced by this Normal
experiment. The unit-cost discrepancy was subsequently resolved by selecting
the correct `Custom_V1` game-data set for the map; the Heroes Used difference
was a comparison error because retail's `3` is Arthas's level label.

### Score diagnostic cleanup (2026-10-14)

Removed investigation-only unit lifecycle tracing and first-frame survivor
audits. Largest Army remains the highest observed food usage, including
scripted setup peaks, without a post-startup reset or scan. Optional bounded
score summaries are compiled via `WC3_DEBUG_SCORE=1` (internal
`BZ_WC3_DEBUG_SCORE`) and gated by `wc3_game_result_debug 1`; gameplay and score
accounting remain independent.

### Retail Heroes Used portraits and current ownership (14 October 2026)

Controlled TFT 1.29.2 experiments show that the Heroes Used column presents
individual hero portraits with their **levels**, not a historical numeric
creation count. In a fresh UndeadX07c run, the `3` belongs to level-3 Arthas;
Anubarak, transferred to Neutral Passive, is not listed under Player 0.

The outgoing score snapshot now scans currently owned hero edicts. Each distinct
instance is represented separately, including duplicate rawcodes. Hidden, dead
and revived heroes remain included; finished `RemoveUnit` and pending deferred
removals are excluded. The snapshot copies entity identity, level and authored
portrait texture before map unload. A unit's owner at snapshot time determines
which player's Heroes page lists it. Hero Score, gained XP and JASS score IDs
are unaffected. The previous persisted `heroes_used` creation counter has been
removed, requiring save version 95.

The generated score row uses the authored unit command-button art, with a
small level label. **Presentation approximation:** native score-row placement
and level badge styling are not available in the supplied FDF. Four portraits
fit in the existing five-column row; larger hero rosters show a `+N` overflow
indicator while retaining the full count for sorting. This is a bounded UI
policy, not a recovered retail overflow rule. Retest the portrait sizes at
4:3 and wide resolutions. Preplaced/cross-mission cached hero edge cases
are still pending separate retail verification.

### Hero level and tab paint order (14 October 2026)

The Heroes Used portrait levels are placed immediately to the right of their
portraits instead of over the bottom-right corner. The FDF tab strip's stock
MDX is a **single composite sprite**; it is drawn in the generic late-sprite
pass, after content. Its tab caption text is drawn in a generic late-text pass
with dedicated flag ranks 3 to 0 (Resources, Heroes, Units, Overview), so
captions remain on top of the model. This changes caption order, **not** the internal
mesh/material order of the stock composite MDX. Verify parchment overlap
visually against retail before treating that part as fixed.

### Score tab caption and composite-geoset correction (14 October 2026)

A screenshot after the previous presentation patch showed `0` instead of the
Units/Heroes/Resources labels: the old late-caption code assigned 1/2/3 to the
`Stat` binding, which the generic client resolves as player-stat text. The
caption paint rank is now transported in distinct high bits of `ui_flags`,
leaving `Stat` and the actual localized text untouched. The existing hero
portrait/level side-by-side layout stays unchanged.

The late text flags retain the four tab captions in Resources/Heroes/Units/
Overview draw order without changing `Stat` bindings. See the endpoint fix
below for the actual cause of the overlapping tab artwork.

### Score tab one-shot animation endpoint (14 October 2026)

Inspection of the retail `ScoreScreen-Tabs.mdx` and its `scorescreen-tabs.blp`
found **five overlapping alpha-key geosets**, one for each `Tab1`–`Tab5`
sequence. Each GEOA track has static alpha 1 and keys with alpha 0 at the start
of every *other* sequence. Thus only the selected tab is meant to be visible;
these are not independent tabs that should be sorted by horizontal position.

The UI sprite path previously clamped a non-looping sequence to its authored
`interval[1]` (for example, 333 for `Tab1`), while
`R_FindSequenceAtTime()` uses an exclusive end. After that hold elapsed, the
frame was outside every sequence: GEOA lookup returned without applying the
inactive tabs' zero-alpha keys, so all five static-alpha-1 geosets were drawn.
Their overlapping quads produce the hard vertical seams seen in screenshots.

One-shot UI sprite sampling now freezes at `interval[1] - 1`, just like the
already end-exclusive `@ratio` path, preserving the current sequence for
geoset and material animation evaluation. The unused opt-in right-to-left
blended-geoset sorting feature was removed. This is a generic sprite animation
fix rather than a score-screen-specific override. Confirm `Tab1`–`Tab4` after
at least 1 second of display and while switching tabs; no build or runtime
verification was performed while preparing the patch.

### Post-game lifecycle Patch 1 — finalization boundary (14 October 2026)

`G_QueueScoreTransition` now records only the first destination. The immutable
result snapshot is captured once in `G_FlushScoreTransition`, after the frame
has drained deferred removals and completed pending scripted saves. In
particular, late-frame unit removal and score changes are not hidden by an
early `EndGame(true)` or `ChangeLevel(..., true)` call. No next-map loading is
submitted before Continue (or the existing missing-UI fallback).

For result-dialog commands processed while script/modal pause is active,
`G_ClientCommand` calls the game-owned paused finalization helper *after* its
synchronous handler has returned. That helper drains terminal result events,
deferred unit/item removals and dependent scripted saves before capturing and
presenting results. Unpaused commands still use the normal `G_RunFrame` tail.
This patch does **not** stop the simulation permanently; match-complete pause
ownership and gameplay command rejection belong to Patch 2. The outgoing
map/game module and its score artwork remain loaded until the existing
Continue transition.

Regression coverage should exercise a score change after queueing and before
finalization, deferred hero removal, a paused result command with no subsequent
server simulation frame, and unchanged unpaused menu deferral.

### Post-game lifecycle Patch 2 — terminal single-player pause (14 October 2026)

After `G_FlushScoreTransition` successfully writes a score window to exactly
one connected viewer in a single-human map, the WC3 module marks the outgoing
match **terminal** and recomputes the existing authoritative server pause. This
is a distinct, irreversible pause reason until map load/shutdown, so a late
`PauseGame(false)` or client modal close cannot restart gameplay. The server
continues processing client network messages while paused; the WC3 command
entry point only accepts score-screen Continue, tab, sort, scroll, teams and
ally-colour intents, while `ClientInput` ignores gameplay camera input.
`G_RunFrame` also refuses to advance a terminal match if another pause owner
unexpectedly releases the server scheduler. The terminal guard survives the
one-shot Continue action until the destination starts loading.

Only a successfully presented single-viewer score screen enters this state.
Missing FDF/no-recipient fallback keeps its existing immediate destination,
and matches with multiple connected clients retain their prior behavior.
This patch does not change score construction, the network wire format, save
format, or engine rendering. Explicit external engine-level `SV_SetPaused(false)`
can still advance the server clock by at most a scheduling tick before the
WC3 frame guard takes effect; preventing that would require a separate generic
pause-ownership contract and is not implemented here.

Headless regression coverage verifies pause ownership after script/modal
release, unchanged gameplay frame/time, blocked client input and gameplay
commands, continued score interaction, multi-client compatibility, and the
Continue-to-new-map guard lifetime. Local compile/runtime verification remains
outstanding.
