# Warcraft III Game Difficulty

## Ownership and contract

OpenRealm has three distinct difficulty concerns:

| Value | Owner | Meaning |
| --- | --- | --- |
| `wc3_campaign_difficulty` | Warcraft III menu via the client's archived cvar service | Profile preference for a **new campaign map**: `0` Easy, `1` Normal (default), `2` Hard |
| `level.setup.difficulty` | Warcraft III game module | Current map difficulty returned by `GetGameDifficulty()`; JASS also supports Insane (`3`) |
| `level.setup.default_difficulty` | Warcraft III game module | Independent default returned by `GetDefaultDifficulty()`; JASS can change it independently |

`games/warcraft-3/common/difficulty.h` is the common interpretation of the
profile string and campaign-path check. The selector and fresh-map initialization
use the same bounded parser; malformed, missing, negative, trailing-text,
over-range, and Insane profile settings all resolve to **Normal**. The parser
accepts decimal leading zeros but not sign characters or whitespace.

The cvar is registered with `CVAR_ARCHIVE` by `M_Init()`, not by loading a map.
`menuImport_t.Cvar_Register` is a generic engine callback: the Warcraft menu
names its preference and the universal client delegates registration to
`Cvar_Get()`. Registering an existing variable adds flags without replacing its
loaded value. The normal `Cvar_WriteConfig` / application-exit path persists it
in the writable user config, without changing the Warcraft game module's
ownership of live simulation difficulty.

## Initial map setup

`G_SpawnEntities()` initializes `level.setup.difficulty` and
`level.setup.default_difficulty` together before `G_InitJassHost()` and script
initialization. For fresh maps in the `Maps\Campaign\` or
`Maps\FrozenThrone\Campaign\` families (slash or backslash, case-insensitive),
the initializer reads the profile preference. Other maps start on Normal,
regardless of that preference. JASS may later change current and default values
independently, so neither is permanently bound to the profile cvar.

## Restart and scripted level transitions

A JASS `RestartGame` or `ChangeLevel` (including a direct `ChangeLevel` without
`SaveGame`) must retain the **current and default** difficulty separately.
`G_RequestRestartGame()` takes the active `level.map_path` as its map identity;
`G_RequestChangeLevel()` uses the script's requested map. Both stage a transient
`wc3DifficultyTransition_t` in the game module and queue the existing deferred
`MenuAction("level")` transport. No map load occurs inside the JASS stack.

The game imports the generic `ResolveMap` capability used by the client menu
router (`Com_ResolveMapArgument`) so a script-supplied basename is staged under
its *resolved full map path*, not under a potentially ambiguous filename.
`G_LoadMap()` passes the actual map filename to `G_SpawnEntities()`, which calls
`G_TakeDifficultyTransition()` **after** clearing the old `level` and before
JASS initialization. The one-shot record is consumed regardless of whether the target matches; only an
exact full-path match (case/slash insensitive) supplies the recorded pair.
Otherwise a fresh map initializes from its campaign preference, or Normal for
non-campaign maps. The record is cleared on game initialization/shutdown and
normal game exits, and explicitly replaced/cleared on competing game-issued
transitions. The universal client never interprets the difficulty values.

`SaveGame` + `ChangeLevel` has an additional ordering rule. `G_FlushScriptSave()`
first commits the source snapshot using `WriteGame()`. **Only after success**
does it stage the transition and send `MenuAction("level")`. A rejected request
or failed source save neither changes maps nor stages a difficulty override.
Script `LoadGame` instead restores the destination save and does not inherit an
outgoing map transition. Transient state is never written into the save.

The original `do_score_screen` argument is still forwarded unchanged through
the existing interfaces; implementing score-screen presentation is separate.

For example, Hard / Hard can become Normal / Hard through a script's
`SetGameDifficulty(Normal)`. Restarting the same map or invoking `ChangeLevel`
then carries Normal / Hard, leaving the archived **Hard** preference untouched.
Stock Blizzard victory continuations can first restore current from default,
so the following chapter enters at Hard / Hard. This patch preserves these
values but does **not** implement the Reduce Difficulty defeat button itself.

## Save and load (format 90)

`g_save.c` now serializes both `level.setup.difficulty` and
`level.setup.default_difficulty` as separate integer level fields. `ReadGame()`
restores the saved pair rather than reconstructing either value from
`wc3_campaign_difficulty`; loading a temporary Normal / Hard run therefore
continues on Normal / Hard even when the menu preference is Easy.

The save format advances from version 89 to **90**, rejecting old snapshots as
required by [Save/load](save-load.md). Transient map-transition records from
Patch 2 are not saved; they are only meaningful when starting the next map.

## Reduce Difficulty after defeat

Stock `CustomDefeatReduceDifficultyBJ` changes *current* difficulty from Hard to
Normal or Normal to Easy, leaves default unchanged, unpauses and restarts the
mission. Script-authored `DialogDisplay` actions own their result presentation,
including this original Blizzard.j action. OpenRealm's native GameResultDialog
fallback now offers a localized **Reduce Difficulty** action for single-player
defeat when the current value is Normal or Hard. It clones the existing
GameResultRestartButton FDF template for consistent styling rather than
requiring a project-owned FDF. The command validates the defeated player,
reduces only the current difficulty and uses `G_RequestRestartGame(true)` so
Patch 2 preserves the current/default pair. It does not change the archived
campaign preference. The fallback hides the action for Easy and Insane; the
stock script dialog may retain its own Insane visibility semantics.

## Completed mission difficulty (Patch 4)

`campaign-progress.orcp` version 2 stores an independent completion bit and
highest successful mission difficulty. On a JASS-authored `RemovePlayer(...,
PLAYER_GAME_RESULT_VICTORY)`, the game resolves the full map path against
CampaignStrings and records the active `level.setup.difficulty` if the human
player's victory was accepted. The developer `win` command is excluded. The
campaign menu appends the localized best difficulty to completed mission names.
Unlocks are unchanged; version-1 availability is migrated without claiming
any mission was completed. See [Campaign progress](campaign-progress.md).


## Verification

`menu_fdf.campaign_difficulty_rejects_invalid_preference_and_isolates_melee_maps`
checks valid/invalid values and both campaign directory families.
`menu_fdf.campaign_difficulty_registration_marks_loaded_setting_archived`
checks registration on menu initialization, preservation of a previously loaded
value, and emission by `Cvar_WriteConfig`. The existing campaign selector tests
exercise setting the preference and its displayed label.
`wc3_difficulty_transition.*` checks independent current/default values,
resolved JASS map basenames, target mismatch one-shot consumption, and failure
to resolve a destination. `wc3_script_save.successful_save_commits_before_next_level`
checks that a completed source save carries the independent pair, while
`wc3_script_save.failed_write_never_starts_next_level` asserts that a failed
save stages no transition. `wc3_save.current_and_default_difficulty_round_trip_independently`
exercises Normal / Hard and Insane / Easy snapshot restoration. `wc3_game`
regression cases cover server-authoritative native defeat reduction and the
fallback button's visibility, styling and command wiring.

See [Campaign progress](campaign-progress.md) for *separate* mission unlock
persistence, [JASS native coverage](../../../games/warcraft-3/jass-native-coverage.md)
for runtime difficulty handles, and [Save/load](save-load.md) for current
serialization boundaries.

## Campaign AI difficulty coverage (Patch 5)

Difficulty remains script-owned. Retail `Scripts/common.ai` observes
`GetGameDifficulty()` when campaign AI initializes; the `Campaign*Ex`
helpers choose their Easy/Normal/Hard argument. Unknown higher levels use the
Hard quantity for these helpers. `SetReplacements` likewise selects one of its
three authored counts. Nothing in the engine should overwrite those values with
a global difficulty multiplier or independently schedule a second set of waves.

`wc3_bot.campaign_difficulty_drives_ai_settings_defenders_and_wave_quantities`
loads a small test AI script in its own JASS VM, once per game difficulty. It
checks that this VM reads current mission difficulty, the three-argument
quantity selection reaches replacement/defender/assault native state, and
Hard-only repair flags are not incorrectly applied at Easy/Normal. The fixture
also checks the Hard quantity fallback for Insane. The script is a focused
contract fixture, **not** the complete retail `common.ai`; it does not prove
real campaign wave timing or readiness.

For mission-specific evidence, use the `--difficulty-scan` mode documented in
[Campaign map audit](map-audit.md#campaign-difficulty-source-inventory-patch-5).
That inventory reports source references and unresolved AI scripts rather than
claiming that a branch was executed. Running it with the installed retail MPQs
and extending the resulting map-specific tests is required before making
broad claims about RoC/TFT difficulty parity.

### Known follow-up: player handicap effect

The current `SetPlayerHandicap` native stores a player percentage in
`game.clients[..].jass.handicap`, and `GetPlayerHandicap` reads it back. The
runtime use of this value in health/max-health calculation is not established
by the current source (unlike `handicap_xp`, which is used in kill XP). The
static audit lists maps that call this native so they can be prioritized. Do
not treat this as a completed gameplay feature or introduce an arbitrary
health multiplier without an authored-unit lifecycle and save/load design.
