# Warcraft III Score Screen

This file describes the score foundation, controller, campaign-quit follow-on,
retail-presentation Patch 1, and dynamic-row Patch 2.
The installed Warcraft assets are required for visual presentation; the project
never bundles Blizzard FDF or score artwork.

## Source and confidence

The raw score counters are simulation-authored as described in the preceding
foundation patch. Five **unverified guesses** now return values through
`GetPlayerScore` and are also shown on the result screen. They are deliberately
separated from the accounting hooks in `g_score.c` and tagged `GUESS`:

| Player score | Temporary algorithm (NOT confirmed retail) |
|---|---|
| Tech percent | 100 × researched technology IDs / observed technology IDs, or 0 with none |
| Units total | 10× trained + 10× enemy units killed + 20× structures built + 20× structures razed |
| Heroes total | 100× hero kills + 10× items gained |
| Resources total | floor(gross gold / 10) + floor(gross lumber / 5) |
| Overall total | Units + Heroes + Resources + 10× Tech percent |

All totals saturate at `INT32_MAX`. **These weights and denominators are guesses,
not reverse-engineered Warcraft calculations**. In particular, the technology
denominator lacks unresearched-but-available tech; hero score lacks XP/hero
usage; and resources do not have authoritative transfer/tax accounting. Compare
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

Only included score-screen players appear. The flag `SetPlayerOnScoreScreen`
is honoured when set on any player; otherwise it falls back to used/connected
slots, to avoid empty screens where the map never initialized the flag.
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

The pages now follow the keys from `GlobalStrings.fdf`:

| Page | Displayed columns |
|---|---|
| Overview | Unit Score, Heroes Score, Resource Score, Total Score |
| Units | Units Produced, Units Killed |
| Heroes | Heroes Used, Heroes Killed, Items Obtained, Mercenaries Hired, Experience Gained |
| Resources | Gold Mined, Lumber Harvested, Resources Traded, Tech Percentage, Gold Lost to Upkeep |

Raw counters from the frozen snapshot populate known columns. For untracked
`Heroes Used`, `Experience Gained`, and `Resources Traded`, show `-`, **not**
a fabricated zero. Technology percentage and overview aggregate scores retain
the explicitly marked `GUESS` formulas in `g_score.c`. Highlighting the
highest positive score per column is an additional **presentation guess**
inferred from the screenshot, not verified retail sorting logic.

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
