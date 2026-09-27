# Branch review: WC3 Night Elf mechanics

Commits reviewed: `407b389a` through `78791e2e` (15 commits), against `main` at `a140572d`.

This review checked the changed code against `AGENTS.md`, `CONTRIBUTING.md`, the linked WC3 mechanics/save documentation, and the relevant authored test fixtures. The findings below distinguish defects from missing regression coverage; fixes should follow the test-first workflow in `CONTRIBUTING.md` and the ownership rules in `AGENTS.md`.

## Findings

### 1. Ancient root state had no save/load round-trip coverage — fixed

`f20df185` adds a persistent `ancient_root` structure and format 51 serializer fields, including a live approach target, transition deadline, morph mode, and collision/defense state. The existing save tests round-trip generic edict pointers and the mine-overlay data, but there is no Ancient Root round-trip test in `t_game.c`; `t_ancient_root.c` only verifies runtime durations, order gates, and a targeting filter. This misses the exact case called out by `CONTRIBUTING.md`: update round-trip coverage when the edict contract changes and save while each new callback/state is live. A load during approach or either morph could therefore lose the transition relationship or resume in an inconsistent form without a regression detecting it.

**Fix applied:** Added a save/load regression in `t_ancient_root.c` that saves while the root and uproot morph moves are active, checks the transition deadline, destination, generation-guarded target pointer, and restored ability-owned move, then advances simulation and checks the committed mode and structure/immobility flags. ROC/TFT AbilityData parsing remains covered by the existing duration test.

### 2. Prior-save rejection coverage omitted formats 49 and 50 — fixed

`407b389a`, `e24cdd09`, `4eceec4a`, and `f20df185` advance the save format through 48, 49, 50, and 51. `g_save.c` rejects every version other than 51, but `wc3_save.rejects_prior_save_versions` originally synthesized only versions 39–48. This did not make the loader accept an old save, but left two incompatible layouts out of the test that guards the version contract.

**Fix applied:** Added versions 49 and 50 and their fixture paths to `rejects_prior_save_versions`. Keep future save-format bumps paired with rejected-old-version fixtures.

### 3. Build-menu button could open an empty build list — fixed

`G_UnitHasBuildMenu()` in `hud_unit.c` checks only that `UnitProfile.builds` is nonempty, and `G_GetCommandButtons()` always exposes `CmdBuild` for such a profile. The new `ui_builds()` then drops options in `BUILD_COMMAND_ABSENT` or `BUILD_COMMAND_HIDDEN` states and emits only Cancel when every authored option is hidden (for example, once all capped/one-time structures are already present). The player can still click the Build button but gets a blank submenu with no valid construction choice.

**Fix applied:** `G_UnitHasBuildMenu()` now checks each authored build entry through `G_GetBuildCommandState()` and shows `CmdBuild` only if an option is not absent or hidden. Disabled and unaffordable options remain visible. Added a command-card test for a profile whose only building is capped.

### 4. Entangle Tree save relationship has no focused round-trip coverage — fixed in working tree

`4eceec4a` adds `mineoverlay.entangle_tree` and its generation guard so an Ancient can find and release its Entangled Mine after uprooting or removal. The pointer is included in `mineoverlay_fields` in `g_save.c`, but the save tests do not assign and round-trip this new pointer. The existing `racial_gold_mine_state_round_trip` covers the mine parent and income state; the generated pointer-field test covers `mineoverlay.caster`, not `mineoverlay.entangle_tree`. This leaves the newly introduced teardown relationship outside the save contract coverage required by `CONTRIBUTING.md`.

**Fix applied:** Added a focused `mineoverlay.entangle_tree` pointer and generation round-trip in `t_game.c`, plus a generic field-schema round-trip. The generation assertion verifies that a reused slot no longer matches the saved Ancient identity. Existing Entangle lifecycle tests cover overlay release and parent restoration.

### 5. Save-format bumps lack the explicit approval required by AGENTS.md — open

The reviewed range advances `save_version` from 47 to 48, 49, 50, and 51 in `407b389a`, `e24cdd09`, `4eceec4a`, and `f20df185`. `AGENTS.md` says the save format must not be bumped without explicit approval. No such approval is recorded in the branch documentation, while `g_save.c` now rejects every version except 51 and the test suite codifies rejection of the prior versions. The serializer changes may require a compatibility decision, but the commits do not document the policy exception.

**Fix plan:** Before treating the bumps as accepted, record explicit approval and the save-compatibility impact in the change documentation. If approval is not granted, redesign the serializer change to preserve the existing version contract and add migration/compatibility coverage rather than rejecting existing saves.

### 6. Ancient Root silently accepts missing AbilityData — fixed in working tree

`ancient_root_duration()` and `S_AncientAttackMask()` read the root ability row without checking that `G_AbilityData(ability)->id` matches the requested rawcode. `G_AbilityLevel()` returns a zero-filled row when AbilityData is absent, so a missing Aroo/Aro1/Aro2 row produces a zero-duration morph and zero enabled attack slots instead of exposing the unresolved data. This conflicts with `AGENTS.md`'s no-silent-fallback rule and can turn a data-loading gap into incorrect gameplay.

**Fix applied:** Ancient morph duration and attack-mask paths now verify the resolved row ID, log missing data, and use safe no-morph/default-mask behavior. `missing_ability_data_does_not_start_morph` exercises a missing concrete row.

### 7. Entangle silently rejects a missing authored overlay UnitID — fixed in working tree

`entangle_goldmine_selecttarget()` reads `G_AbilityLevel(alias, 1)->unitID` and returns `false` when it is zero. A missing or incomplete Aent AbilityData row therefore leaves the player in target selection with no command error or diagnostic. This silently drops unresolved authored data, contrary to `AGENTS.md`'s logging rule.

**Fix applied:** The selection callback now checks the Aent row identity and `UnitID` before range evaluation/spawn, logs the unresolved rawcode/field, and shows target-mode error feedback. Added a missing-UnitID regression that checks no overlay is spawned and target selection remains active.

### 8. Save format changes violate the unapproved wire-format policy — requires explicit approval

The branch still advances WC3 save format from 47 through 51, including additions for Entangle and Ancient Root state. `AGENTS.md` explicitly requires bit-compatible wire format unless approved and says there is no save-format version bump. I found no approval in the branch or its documentation. The serializer rejects every version except 51, so existing saves are incompatible. This cannot be resolved by merely updating a test or comment: preserving existing save files needs a deliberate compatibility/migration design, while retaining format 51 requires an authorized exception.

**Fix plan:** Obtain explicit approval for the compatibility break or implement an approved compatible extension/migration; document the exact save impact in the change description and update `docs/games/warcraft-3/save-load.md` to the actual current format. Do not label this fixed until the compatibility decision is made.

## Review checks

- The fixes update production code and focused regression coverage; the save-format policy item still needs an explicit compatibility decision.
- The follow-up review covered `f2ebff92` and `78791e2e`; no additional runtime defect was confirmed in the Moon Well update fast path or the build-menu correction. The open items above cover the remaining save-coverage, authored-data diagnostics, and policy issues.
- The commit-range `git diff --check` is clean.
- Tests and game launch have not been run in this pass.
