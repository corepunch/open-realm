# Warcraft III Score Screen

This file describes the implementation after the score-controller follow-on patch.
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
client-owned pause semantics. Four page buttons switch between Overview, Units,
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

### FDF binding parity limitation

The current repository contains no licensed `ScoreScreen.fdf` bytes to inspect,
so only the known `ScoreScreenFrame` root is bound. Other dynamic labels and
buttons currently borrow existing in-game result FDF text/button styling, and
their anchored positions are marked **PRESENTATION GUESSES** in
`hud_score_screen.c`. **This is a functional preliminary score overlay, not yet
pixel-accurate retail tab/column binding.** Once retail FDF bytes are supplied,
use `fdfbindgen` to bind its actual named tab, column and button controls and
remove the provisional row/tab positioning; do not create a project FDF copy.

## Manual validation needed

- Launch a finished single-player mission with `EndGame(true)` and inspect
  actual retail layout/font sizes and Continue routing.
- SaveAndChangeLevelBJ: confirm that the script save commits *before* the score
  modal opens and Continue loads the intended next map once.
- `ChangeLevel(..., false)` still moves directly, never opening the modal.
- Multiplayer: every connected participant sees frozen columns; a Continue
  from any displayed recipient releases the shared server destination.
- Compare guessed aggregate values against retail under controlled matches.
- Ensure a missing `ScoreScreen.fdf` proceeds to the destination.

Compilation, gameplay, rendering and the above observations remain for the
local build/test run. `AGENTS.md`, `CONTRIBUTING.md` and `docs/ui-authoring.md`
remain authoritative.
