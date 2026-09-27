# Branch coverage review

Reviewed commits `407b389a` through `78791e2e` (the 15 requested commits) against the parent `a140572d`. This is a source-level coverage review; I did not run tests or collect runtime branch coverage.

## Overall assessment

Coverage is substantial for the intended happy paths and several important regressions. Harvesting has wide simulation coverage, Ancient rooting has dedicated order/timing/target tests, building command restoration has command-card tests, and Moon Well has data-driven behavior plus cleanup/performance checks. `78791e2e` addresses the prior save/load gap for active morphs, old-format rejection fixtures, and the empty build-menu case. The new focused Root tests now cover blocked placement and the successful placement-to-arrival-to-morph flow. Renderer coverage still verifies the exact splat gate used in entity drawing, but not framebuffer output.

## Findings

### Remaining coverage limits

1. **Renderer framebuffer output is not covered.** The test exercises `R_ShouldRenderUberSplat`, the exact predicate called by `R_RenderUberSplat` during entity drawing, including structure, no-splat, and no-structure cases. This covers the branch decision. A framebuffer-level test would be needed only if visual placement/compositing of the splat itself is part of the required contract.
2. **Ancient save/load during an actual approach remains uncovered.** The `78791e2e` test resumes root and uproot morphs and checks the saved approach target pointer/generation fields, but writes those target fields into a morph state manually. It does not save while the Ancient is walking toward a chosen root location. The successful approach test added here covers runtime arrival; save/load coverage currently targets the morph phase.

2. **The Ancient visual effect is tested only as a predicate.** `5cdab4f4` adds renderer filtering for rooted structure splats and `tests/test_renderer_view.c` verifies that the helper tracks a flag. The test does not exercise the entity submission/render path with a real Ancient mode transition, so it does not establish that the splat is actually omitted and restored in rendered output. If this is intended as a pure selector contract, the unit test is adequate; otherwise add a renderer-level submission test.

### Addressed by `78791e2e`

- **Ancient morph save/load:** `root_and_uproot_morphs_resume_after_save_load` now round-trips active root and uproot morphs, checks transition deadline, destination, guarded target identity, move restoration, and final mode/structure/immobility state. This materially closes the prior gap for morph saves. It does not verify all serialized Ancient fields individually (for example collision snapshots and rooted ability/unit identity), nor does it save while a real root approach is active.
- **Prior save versions:** `rejects_prior_save_versions` now includes formats 49 and 50, covering the gap in the incompatible-format fixture list.
- **Empty Build submenu:** `build_menu_button_is_hidden_when_every_build_is_hidden` verifies that a capped-only build list hides the button while an unaffordable option keeps it available. This covers the new `G_UnitHasBuildMenu` command-card decision.

### Added in this review

- `command_rejects_blocked_placement_and_keeps_cursor_active` starts Root through `A_COMMAND`, obtains an open snapped point, blocks it with a live unit, then submits the point through the registered location callback. It asserts the placement is rejected, the Ancient remains uprooted, and location targeting stays active for a retry.
- `placement_order_walks_then_starts_root_morph_on_arrival` starts Root through the ability command, submits a valid location, verifies the unit receives a Move goal and generation-guarded approach target, then dispatches `A_MOVE_ARRIVE` and verifies the authored root morph begins.
- Fixed the `button_capacity` argument in the existing `78791e2e` build-menu test to avoid the repository's `ARRAY_COUNT` macro requiring an undeclared `<array>_count` companion.
- Updated Entangle regression fixtures to include actual `Aro1` ability state and rooted mode, matching the new `S_AncientIsRooted` contract. Updated the mobile-builder command-menu fixture with UnitProfile ownership and building UnitBalance rows so the new availability filter sees valid choices.

### Covered behavior

- `407b389a`, `e24cdd09`, `bba63abb`: Wisp/worker harvesting tests cover order dispatch, periodic resource credit, stock and ROC data columns, missing data, owned-tree retargeting, cancellation, mine capacity/depletion, and entangled mine income/unload lifecycle.
- `4eceec4a`: Entangled Mine coverage includes caster generation/liveness and overlay restoration; save tests round-trip the newly persisted MineOverlay caster and permanent-state fields.
- `f20df185`, `3fa6a15b`, `63fdae6c`, `78791e2e`, and this review: Ancient tests cover distinct root/uproot authored durations for ROC and TFT, rejection of gameplay orders during both morphs, completion at the authored deadline, save/load continuation of both morph directions, blocked placement rejection, successful placement and arrival into morph, interruptible approach, runtime spell structure classification, and ability availability dispatch. The later fixture correction ensures the target matches the expected runtime class.
- `692c48a9`, `b258a70f`, `57eb9db9`, `78791e2e`: Building tests cover town-hall/Tree of Life train and upgrade buttons, mobile builders retaining race build menus, producer profile behavior, renderer HUD button generation against fixture data, and hiding a build menu when all choices are unavailable by design.
- `6bcdabce`: The rooted Ancient command button test verifies Uproot art/tooltip selection.
- `f5d9d168`: Rally tests cover rooted-only rally behavior and normal smart-order handling for an uprooted Ancient.
- `f2ebff92`: Moon Well tests verify ordinary ability updates avoid cleanup scans while disabling the actual Moon Well ability still releases its effect; adjacent tests cover autocast selection, authored thresholds/area/range, natural regen time-of-day behavior, and ROC columns.

## Validation notes

- Reviewed the test additions and production diffs for all requested commits.
- Focused Ancient Root tests pass (102 assertions), renderer view tests pass (141 assertions), and the Entangle and mobile-builder regressions pass independently.
- The full `make test` run after fixture corrections completes the WC3 engine suite with 35,547/35,568 assertions and 21 failures. The new Root placement, Root save/load, updated Entangle, and mobile-builder tests pass. Remaining failures are in ability lifecycle (Blizzard reduction/Mass Teleport), minimap contact publication, Acolyte construction/unsummon, Monsoon/Earthquake damage, Human07 JASS removal, Burrow command state, and structure decay tests. These need separate triage before the full suite is green; I did not change their behavior as part of this coverage update.
- `git diff --check` passes for the edited files.
