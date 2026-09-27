# Branch review: WC3 Night Elf mechanics

Commits reviewed: `407b389a` through `5cdab4f4` (13 commits), against `main` at `a140572d`.

This review checked the changed code against `AGENTS.md`, `CONTRIBUTING.md`, the linked WC3 mechanics/save documentation, and the relevant authored test fixtures. The findings below distinguish defects from missing regression coverage; fixes should follow the test-first workflow in `CONTRIBUTING.md` and the ownership rules in `AGENTS.md`.

## Findings

### 1. Ancient root state had no save/load round-trip coverage — fixed

`f20df185` adds a persistent `ancient_root` structure and format 51 serializer fields, including a live approach target, transition deadline, morph mode, and collision/defense state. The existing save tests round-trip generic edict pointers and the mine-overlay data, but there is no Ancient Root round-trip test in `t_game.c`; `t_ancient_root.c` only verifies runtime durations, order gates, and a targeting filter. This misses the exact case called out by `CONTRIBUTING.md`: update round-trip coverage when the edict contract changes and save while each new callback/state is live. A load during approach or either morph could therefore lose the transition relationship or resume in an inconsistent form without a regression detecting it.

**Fix applied:** Added a save/load regression in `t_ancient_root.c` that saves while the root and uproot morph moves are active, checks the transition deadline, destination, generation-guarded target pointer, and restored ability-owned move, then advances simulation and checks the committed mode and structure/immobility flags. ROC/TFT AbilityData parsing remains covered by the existing duration test.

### 2. Prior-save rejection coverage omitted formats 49 and 50 — fixed

`407b389a`, `4eceec4a`, and `f20df185` advance the save format through 48, 50, and 51 (with the Entangle change introducing format 49 and the generation-guarded relationship advancing to 50). `g_save.c` now rejects every version other than 51, but `wc3_save.rejects_prior_save_versions` still synthesizes only versions 39–48. This does not currently make the loader accept an old save, but leaves two incompatible layouts out of the test that guards the version contract.

**Fix applied:** Added versions 49 and 50 and their fixture paths to `rejects_prior_save_versions`. Keep future save-format bumps paired with rejected-old-version fixtures.

### 3. Build-menu button could open an empty build list — fixed

`G_UnitHasBuildMenu()` in `hud_unit.c` checks only that `UnitProfile.builds` is nonempty, and `G_GetCommandButtons()` always exposes `CmdBuild` for such a profile. The new `ui_builds()` then drops options in `BUILD_COMMAND_ABSENT` or `BUILD_COMMAND_HIDDEN` states and emits only Cancel when every authored option is hidden (for example, once all capped/one-time structures are already present). The player can still click the Build button but gets a blank submenu with no valid construction choice.

**Fix applied:** `G_UnitHasBuildMenu()` now checks each authored build entry through `G_GetBuildCommandState()` and shows `CmdBuild` only if an option is not absent or hidden. Disabled and unaffordable options remain visible. Added a command-card test for a profile whose only building is capped.

## Review checks

- The fixes update production code and focused regression coverage.
- The commit-range `git diff --check` is clean.
- Tests and game launch have not been run in this pass.
