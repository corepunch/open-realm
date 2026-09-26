# Contributing

For repository webhook formatting and local payload checks, see [Discord notifications](docs/discord-notifications.md).

## Developer Documentation

Investigation findings are part of the contribution. Follow the
[Documentation Guide For Agents](docs/documentation-guide.md) whenever implementation work requires reconstructing behavior from
code, game data, runtime logs, history, issues, or external references. Add focused documents to the nearest subsystem index so the
next contributor can find the answer without repeating the lookup.

## Test-First Behavior Verification

Prefer added regression tests over launching the game to verify gameplay functionality. Reproduce the situation that
would otherwise require playing a map, issuing console commands, or watching a cinematic. Headless tests can execute
the real game module through the test binary; this does not require opening the game or using installed retail archives.

1. Establish the expected contract from authoritative data and the actual code path. Describe the initial state, inputs,
   event order, timing, and expected observable result.
2. Add the smallest representative fixture and drive production entry points: orders, script/native dispatch, scheduler
   ticks, animation end callbacks, save/load, or message encode/decode, as relevant. Advance simulation time explicitly.
   Avoid wall-clock sleeps, copies of production algorithms, or mocks that replace the behavior being verified.
   A non-stock Data cell is required so the test cannot pass on a hardcoded retail constant; it does not replace a
   stock-shaped case, omitted ROC column, or the brief's actual BuffID/`targs`. Assert the gameplay lock that should
   have ended, not only a buff or helper the execute path already set.
3. Reproduce the failure before fixing it. Include the relevant interruption, pause, inverse, invalid-target, or restoration
   case; success-path helper tests alone do not establish that the gameplay lifecycle works.
4. Fix the owner, rerun the regression, then run the required suite. For WC3 data changes, cover both ROC and TFT schemas
   with fixtures; running the same TFT-only fixture twice does not test ROC column names.
5. If the harness lacks something needed to reproduce the situation, extend the harness, fixture generator, or diagnostic
   tool. Once an automated reproducer covers the behavior, use it for subsequent edits and rebases instead of replaying the game.

The Raven Form refactor provides concrete examples in `games/warcraft-3/game/tests/`: `t_unit.c` issues a morph followed
by Move before animation completion and checks that ascent continues without replacing Move; it also covers pause and
reversal. `t_combat.c` tests an animation end callback installing another sequence, `t_slk.c` covers ROC rawcode columns,
and `t_game.c` checks persistence. The generated `mdxgen morph` model supplies the required sequences without retail assets.
See [ability implementation](docs/games/warcraft-3/ability-implementation-plan.md) for ownership and test design.

Launch the game only for a specific property that remains untestable after considering these options, such as framebuffer
output or OS input integration. State that coverage gap before launching, keep the run bounded with `+com_frame_limit N`,
and capture just the relevant behavior. A newly discovered gameplay failure should become a regression test so another
manual run is unnecessary. Passing behavior tests do not prove visual correctness; report that limit when it matters.
There is no blanket requirement to launch the game after an implementation, rebase, or build.

For code and executable fixture/build changes, build the affected targets and run `make test` before committing.
Documentation-only changes need text, relative-link, and `git diff --check` validation; no game launch or full suite is needed.

## Test Fixtures and MPQ Assets

- Tests must not depend on a developer's local Warcraft III data or `War3.mpq`. Add game-specific archive fixtures under `games/<game>/tests/resources-src`; Warcraft III packs `games/warcraft-3/tests/resources-src` into the generated `build/tests/tests.mpq` through `make test-assets`, and tests should read from that fixture MPQ instead.
- Tests must not read from ignored local extraction folders such as `data/fdf` or `data/Warcraft III`. If a Warcraft III test needs FDF, map, texture, model, or other archive content, copy the minimal fixture into `games/warcraft-3/tests/resources-src`, add it to `build/tests/tests.mpq`, and read it from that generated archive.
- When a test fixture intentionally replaces an actual game archive file with custom content, use the same archive path and filename as the real game file. Do not invent project-specific replacement names for files that are meant to stand in for game files; keep the name WoW/Warcraft-style and make only the contents custom.

## Test Framework

All tests use `shared/test.h`. A test self-registers at load time via `__attribute__((constructor))` — no `main()` or manual `RUN_TEST()` needed:

```c
#ifdef BZ_TESTS
#include "shared/test.h"
TEST(suite_name, test_name) {
    T_EQ(actual, expected);
    T_STREQ(actual_str, expected_str);
    T_FEQ(a, b, 0.001f);
}
#endif
```

The registry lives in `libshared` so game-module constructors register into the same list as engine tests. Run with `+dedicated 1 +test '<pattern>'` where pattern supports `*` wildcards and `suite.*` prefix matching.

Available assertions: `T_ASSERT(cond)`, `T_EQ(a,b)`, `T_NE(a,b)`, `T_FEQ(a,b,eps)`, `T_STREQ(a,b)`, `T_NULL(p)`, `T_NOT_NULL(p)`.

Warcraft III tests share `alloc_test_unit()` from `games/warcraft-3/game/tests/t_utils.c`. For IDs backed by a real `UnitBalance` row, the helper initializes current/max health to the authored `maxHealth`, matching the live-unit contract of `SP_SpawnUnit`. Tests that need a dead unit must set `health.value = 0` explicitly; otherwise selection and order validation will correctly reject the fixture through `M_IsDead()`.
`setup_test_world()` installs a mutable synthetic `mapInfo_t`, but production exposes it through `level.mapinfo` as `mapInfo_t const *`. Tests that need to configure synthetic player-slot metadata must cast that fixture view back to `mapInfo_t *` (for example `((mapInfo_t *)level.mapinfo)->players[1].playerType = ...`) rather than assigning through the const production pointer.

Assertion failures always include `__FILE__` and `__LINE__`. Under GitHub Actions, the runner also emits a workflow error annotation so failures are clickable at the originating source line.

`make test` also writes one JUnit XML report per `shared/test.h` suite invocation under `build/tests/junit/`. The test runner enables the same output for direct runs when `TEST_JUNIT=/path/to/report.xml` is set; `TEST_JUNIT_SUITE=name` optionally overrides the `<testsuite>` name. Reports are separate per executable/in-engine mode so parallel `TEST_JOBS` runs never share a writable XML file. Shell-only checks such as `test-jass-build` continue to report through their process exit status and console output. The `CI` workflow uploads the raw reports as the `junit-test-results` artifact. A separate `workflow_run` workflow downloads that artifact and publishes the `OpenRealm Unit Tests` GitHub Check against the tested commit. Keeping publication separate allows fork pull requests to receive a real Check without granting the untrusted test workflow a write-capable token; the privileged report workflow must not check out or execute pull-request code. The artifact upload, cross-run download, and JUnit publisher actions used by this path are pinned to immutable commit SHAs.

### Warcraft III Save/Load

The WC3 serializer follows the Quake 2 `g_save.c` pattern but writes a versioned envelope and converts `F_EDICT` references to entity indexes. Keep `games/warcraft-3/game/g_save.c`'s `field_t fields[]` synchronized with every persistent pointer in `struct edict_s`. Edict C callbacks use `F_CFUNCTION` and must be listed in `save_cfunctions[]`; JASS `F_FUNCTION` stays name-string identity for timers/triggers. Update the round-trip test whenever the edict contract changes, and save while each new think pointer is live — `G_RunEntities` does not prove `WriteGame`. See [WC3 Save/Load](docs/games/warcraft-3/save-load.md).

Do not include `test_framework.h` — it has been removed. Do not write a `main()` for test files; link against `tests/test_runner.c` instead.

### Message Delta Tests

Every field or flag added to `entityState_t` travels over the network via `MSG_WriteDeltaEntity`/`MSG_ReadDeltaEntity`.
Write a round-trip test whenever you add a new field, flag, or packed value to confirm the serialization contract:

```c
TEST(wow_appearance, entity_delta_preserves_my_flag) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 9, .model = 3, .flags = EF_MY_FLAG }, out = { 0 };
    uint32_t bits = 0;
    int number;

    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 9);
    T_ASSERT(out.flags & EF_MY_FLAG);
}
```

Key points:
- Set `from` to all-zero and `to` to only the fields under test so the delta is minimal.
- Always set a non-zero `model` alongside new fields — a zero-model entity may be skipped by the delta encoder.
- Reset `sb.readcount = 0` between write and read; `make_msg_buf` is defined in the same test file.
- These tests live in `games/world-of-warcraft/tests/test_wow_appearance.c` for WoW entity fields.
- After verifying network survival, test the game-logic side separately (see below).

### Unit Behaviour Tests via `CustomizeEntity`

Server-side per-client state filtering happens in `Wow_CustomizeEntity` (called via `game->CustomizeEntity`).
Test that function by driving game state, calling it with a copied `entityState_t`, and asserting the resulting flags:

```c
TEST(wow_game, my_feature_sets_correct_flags) {
    struct game_export *game = init_game();
    entityState_t state;
    edict_t *npc = /* find or spawn the entity */;

    /* Drive the entity into the desired server state here */

    state = npc->s;
    game->CustomizeEntity(0, npc, &state);   /* player 0 */
    T_ASSERT(state.flags & EF_MY_FLAG);
    T_ASSERT(!(state.flags & EF_OTHER_FLAG));

    if (game->Shutdown) game->Shutdown();
}
```

Key points:
- `CustomizeEntity` receives a *copy* of `npc->s` and mutates it; `npc->s` is unchanged.
- Call it once per logical state transition (e.g. before quest accept, after quest accept) to test each branch.
- Combine with `wow_clients[player].selected_entity` changes to test visibility gating.
- These tests live in `games/world-of-warcraft/tests/test_wow_game.c`.

## Build and Linking

The Linux CI build and test jobs use `ghcr.io/corepunch/open-realm-ci:ubuntu-24.04`, built from
`.github/docker/ci/Dockerfile`. Keep Linux build dependencies in that Dockerfile rather than installing them in each job or relying
on software preinstalled on the GitHub-hosted runner. The `prepare-linux-image` job checks only that Dockerfile: when it changes on
`main`, CI builds and publishes the replacement image before the Linux test/build jobs start; ordinary commits reuse the existing
image without running APT. Pull requests that change the Dockerfile build a local candidate and run the Linux tests/build inside it,
but do not publish it. `.github/workflows/ci-image-refresh.yml` also rebuilds the published image once per day with `--pull --no-cache`
so unchanged Dockerfiles still pick up current Ubuntu base and APT security updates; it can also be run manually. The same published
image is used for the Linux release build. `--no-install-recommends` keeps optional packages out, and deleting `/var/lib/apt/lists`
keeps the published image smaller. The isolated Ubuntu image also avoids host APT
repository failures such as CI #1487. The Flatpak release path uses Flathub's matching `freedesktop-25.08` build image instead of
installing Flatpak tooling with APT on the runner.

- Never add `DYLIB_LOOKUP := -Wl,-undefined,dynamic_lookup` or otherwise rely on `-Wl,-undefined,dynamic_lookup` in this repository.
- If a target has unresolved symbols, fix the dependency graph or shared implementation instead of weakening the linker contract.
