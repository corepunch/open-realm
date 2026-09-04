# Contributing

## Developer Documentation

Investigation findings are part of the contribution. Follow the
[Documentation Guide For Agents](docs/documentation-guide.md) whenever implementation work requires reconstructing behavior from
code, game data, runtime logs, history, issues, or external references. Add focused documents to the nearest subsystem index so the
next contributor can find the answer without repeating the lookup.

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
`setup_test_world()` installs a mutable synthetic `MAPINFO`, but production exposes it through `level.mapinfo` as `LPCMAPINFO`. Tests that need to configure synthetic player-slot metadata must cast that fixture view back to `LPMAPINFO` (for example `((LPMAPINFO)level.mapinfo)->players[1].playerType = ...`) rather than assigning through the const production pointer.

Assertion failures always include `__FILE__` and `__LINE__`. Under GitHub Actions, the runner also emits a workflow error annotation so failures are clickable at the originating source line.

`make test` also writes one JUnit XML report per `shared/test.h` suite invocation under `build/tests/junit/`. The test runner enables the same output for direct runs when `TEST_JUNIT=/path/to/report.xml` is set; `TEST_JUNIT_SUITE=name` optionally overrides the `<testsuite>` name. Reports are separate per executable/in-engine mode so parallel `TEST_JOBS` runs never share a writable XML file. Shell-only checks such as `test-jass-build` continue to report through their process exit status and console output. The `C/C++ CI` workflow uploads the raw reports as the `junit-test-results` artifact. A separate `workflow_run` workflow downloads that artifact and publishes the `OpenRealm Unit Tests` GitHub Check against the tested commit. Keeping publication separate allows fork pull requests to receive a real Check without granting the untrusted test workflow a write-capable token; the privileged report workflow must not check out or execute pull-request code. The artifact upload, cross-run download, and JUnit publisher actions used by this path are pinned to immutable commit SHAs.

### Warcraft III Save/Load

The WC3 serializer follows the Quake 2 `g_save.c` pattern but writes a versioned envelope and converts `F_EDICT` references to entity indexes. Keep `games/warcraft-3/game/g_save.c`'s `field_t fields[]` synchronized with every persistent pointer in `struct edict_s`. Edict C callbacks use `F_CFUNCTION` and must be listed in `save_cfunctions[]`; JASS `F_FUNCTION` stays name-string identity for timers/triggers. Update the round-trip test whenever the edict contract changes. See [WC3 Save/Load](docs/games/warcraft-3/save-load.md).

Do not include `test_framework.h` — it has been removed. Do not write a `main()` for test files; link against `tests/test_runner.c` instead.

### Message Delta Tests

Every field or flag added to `entityState_t` travels over the network via `MSG_WriteDeltaEntity`/`MSG_ReadDeltaEntity`.
Write a round-trip test whenever you add a new field, flag, or packed value to confirm the serialization contract:

```c
TEST(wow_appearance, entity_delta_preserves_my_flag) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 9, .model = 3, .flags = EF_MY_FLAG }, out = { 0 };
    DWORD bits = 0;
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
    LPEDICT npc = /* find or spawn the entity */;

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

- Never add `DYLIB_LOOKUP := -Wl,-undefined,dynamic_lookup` or otherwise rely on `-Wl,-undefined,dynamic_lookup` in this repository.
- If a target has unresolved symbols, fix the dependency graph or shared implementation instead of weakening the linker contract.
