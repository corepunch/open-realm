# Lightweight In-Engine Testing

Status: proposal / design. No code written yet.

## The problem

Adding a game-module test today costs a lot of boilerplate. Concretely, for every
WoW/SC2/WC3 test we pay three separate taxes:

### 1. Re-implementing the game inside the test

`games/world-of-warcraft/tests/test_wow_combat.c` re-declares the game's globals and
hand-writes stub bodies for real game functions:

```c
struct game_import gi;
struct game_export globals;
edict_t wow_edicts[WOW_MAX_EDICTS];
wowEntityLocal_t wow_entity_locals[WOW_MAX_EDICTS];

DWORD Wow_EntityIndex(LPCEDICT ent) { ... }        /* copy of real impl */
BOOL  Wow_SetEntityMove(LPEDICT, LPWOWMOVE) { ... } /* copy of real impl */
void  Wow_AdvanceEntityFrame(LPEDICT ent) { ... }   /* copy of real impl */
```

The WC3 side is worse: [games/warcraft-3/tests/test_harness.c](../games/warcraft-3/tests/test_harness.c)
is ~500 lines that owns every global `g_main.c` normally owns (`gi`, `globals`, `game`,
`level`, `g_edicts`), mocks the entire `game_import` table, and hand-builds fake SLK
tables field-by-field (`setup_test_unit_data`). These copies drift from the real code,
so a passing test no longer proves the shipping code path works.

### 2. Hand-maintained Makefile source lists

Each test enumerates the exact `.c` files it needs to link:

```make
$(eval $(call test_schema,test-wow-abilities,,$(WOW_TEST_CFLAGS),$(BIN_DIR)/test_wow_abilities...,
    $(WOW_TEST_DIR)/test_wow_abilities.c $(WOW_DIR)/game/g_wow.c $(WOW_DIR)/game/g_world.c \
    $(WOW_DIR)/game/g_ai.c $(WOW_DIR)/game/m_creature.c $(WOW_DIR)/game/g_wow_gameobject.c \
    common/mpq.c $(call CSRC,shared),-lm -lz,))
```

WC3 keeps a parallel `TEST_GAME_SRCS` list of ~30 files. Add a source file to the game
and you must remember to append it here or get link errors. There are already **8+**
separate WoW/SC2 test binaries plus the WC3 mega-binary, each with its own recipe.

### 3. Separate `main()` + counter wiring per binary

Every binary defines `int _tests_run; int _tests_failed;`, a `main()` that forward-declares
and calls each `run_*_tests()`, and its own suite plumbing
([test_main.c](../games/warcraft-3/tests/test_main.c)).

**Net effect:** writing one new assertion about combat can mean touching a test `.c`, a
harness `.c`, a `main()`, and the Makefile — and even then it tests a mock, not the game.

## The goal

- Adding a test = drop one `.c` file with a `TEST(...)` block. No Makefile edit, no
  `main()`, no global re-declarations, no mocked game functions.
- Tests exercise the **real, fully-assembled game module** through the **real**
  `game_import` table — the same `GetGameAPI` path the shipping binary uses.
- One invocation surface: `+test <pattern>`.
- Production builds contain zero test code.

## Core idea

We already have everything the user is asking for; it just isn't wired for testing:

- The engine loads the real game via `GetGameAPI(&import)` in
  [SV_InitGameProgs](../server/sv_game.c) and runs it headless in **dedicated** mode
  ([common/main.c](../common/main.c), `+dedicated 1`) with no renderer / UI / SDL window.
- Modules already self-describe through the `game_import` / `game_export` contract.

So instead of re-linking a subset of game files against mocks, **boot the real game
library under a headless server and run tests as game-side hooks**. The engine provides
a tiny test runtime; game tests just `#include "test.h"` and use it.

```
openwow -data <data> +dedicated 1 +test wow_combat.*
   └─ SV_Init → SV_InitGameProgs → GetGameAPI (REAL libgame-wow, REAL gi)
        └─ Cmd "test wow_combat.*" → Test_Run(pattern)
             └─ runs every registered TEST() whose name matches, prints results
                  └─ Com_Quit(failures ? 1 : 0)
```

`gi` is the real server import table (real `MemAlloc`, `ReadFile` from MPQ, `LinkEntity`,
SLK tables loaded from the archive). The mock harness disappears.

## Components

### A. Engine test core — `shared/test.h` + `shared/test.c`

One dependency-free unit built into **libshared**. This placement is load-bearing: the
game module is a separate dynamic library, so the registry list head and counters must
live in a library both the executable and the game module link (libshared) — otherwise
each dylib gets its own list and the engine's `+test` never sees the game's tests. It
provides the registry, assertion macros, and the runner, replacing both
`tests/test_framework.h` and the per-binary `main()` wiring. The `test` console command
itself is registered in `common/common.c` (`Com_Test_f`), which calls `Test_Run` and
exits with the failure count.

```c
/* common/test.h */
#ifndef test_h
#define test_h

typedef struct test_s {
    LPCSTR        name;      /* "wow_combat.pain_animation" */
    void        (*fn)(void);
    struct test_s *next;
} test_t;

void Test_Register(test_t *t);          /* linked-list insert, called by constructor */
int  Test_Run(LPCSTR pattern);          /* run matches; return failure count */
void Test_Fail(LPCSTR file, int line, LPCSTR expr);

extern int test_asserts, test_failures; /* live counters for the current test */

/* Self-registration: a file-scope constructor links the test in at load time,
 * whether the owning translation unit is in the game .dylib or dlopen'd later.
 * suite/name are separate identifiers so the C token paste is valid while the
 * display name stays dotted ("suite.name") for pattern matching. */
#define TEST(suite, name)                                                  \
    static void suite##_##name##_fn(void);                                 \
    static test_t suite##_##name##_node = { #suite "." #name,              \
                                            suite##_##name##_fn, 0 };      \
    __attribute__((constructor)) static void suite##_##name##_reg(void) {  \
        Test_Register(&suite##_##name##_node);                             \
    }                                                                      \
    static void suite##_##name##_fn(void)

#define T_ASSERT(cond) do { test_asserts++;                                \
    if (!(cond)) Test_Fail(__FILE__, __LINE__, #cond); } while (0)
#define T_EQ(a,b)      T_ASSERT((a) == (b))
#define T_FEQ(a,b,eps) T_ASSERT(fabsf((float)(a)-(float)(b)) <= (eps))
#define T_STREQ(a,b)   T_ASSERT((a) && (b) && strcmp((a),(b)) == 0)

#endif
```

The engine registers the command once in `Com_Init`:

```c
Cmd_AddCommand("test", Com_Test_f);
/* Com_Test_f: exit(Test_Run(argc > 1 ? argv[1] : "*") ? 1 : 0); */
```

Because `+test` triggers `Com_Quit`, the run is synchronous and self-terminating — no
`+com_frame_limit` needed. (For tests that must advance simulation, the test body calls
`ge->RunFrame()` directly, keeping control explicit.)

### B. Tests live next to the code, compiled into the game module

Put game tests in `games/<game>/game/tests/*.c`. The game library is already built by a
UNITY glob over `games/<game>/game`, so these files are picked up **automatically** — no
Makefile source list. Guard them so production excludes them:

```c
/* games/world-of-warcraft/game/tests/t_combat.c */
#ifdef BZ_TESTS
#include "test.h"
#include "game/g_wow_local.h"

TEST(wow_combat, lethal_attack_triggers_death_state) {
    LPEDICT attacker, target;
    combat_prepare(&attacker, &target);          /* real edicts, real player model */
    Wow_EntityLocal(target)->health = 1;
    Wow_AIAttack(attacker);                       /* REAL AI function, not a copy */
    while (Wow_EntityLocal(attacker)->attack_damage_time > 0)
        Wow_AIAdvanceLockedFrame(attacker);
    T_ASSERT(Wow_EntityLocal(target)->dead);      /* asserts real behaviour */
}
#endif
```

Build the game lib twice-capable: pass `-DBZ_TESTS` (and link `common/test.c`) only for
the test build. Tests call real game functions and the real `gi`, so nothing is mocked
and nothing drifts.

### C. Headless boot allows `+test` without a map

Dedicated mode currently hard-requires `+map`. Relax that: if `+test` is queued, boot the
server, init the game, run tests, exit — a fixture map is loaded only by tests that need
world state (`ge->LoadMap("Maps/Test/Tiny...")` against the existing fixture MPQs). Pure
unit tests (SLK parsing, math, pathfinding, `msg` round-trips) register the same way and
run without touching a map.

### D. `make test-<game>` becomes a one-liner per game

```make
# Build the game lib with tests compiled in, boot headless, run all tests.
test-wow-engine: $(WOW_TEST_BINARY)
	$(WOW_TEST_BINARY) -data $(WOW_INSTALL_DATA_DIR) +dedicated 1 +test '$(PATTERN)'
```

`$(WOW_TEST_BINARY)` (`openwow-tests`) is the normal binary linked against a
`libgame-wow-test` built with `-DBZ_TESTS` + `shared/test.c` (already in libshared). One
recipe covers every WoW test; adding a `.c` under `game/tests/` needs no Makefile change.
Filter with `make test-wow-engine PATTERN='wow_combat.*'`.

## Status: proof of concept landed

The mechanism is implemented and green:

- [shared/test.h](../shared/test.h) + [shared/test.c](../shared/test.c) — registry, macros, runner.
- `test` command + headless `+test` boot in [common/common.c](../common/common.c) and [common/main.c](../common/main.c).
- `libgame-wow-test` / `openwow-tests` / `test-wow-engine` targets in the [Makefile](../Makefile).
- [games/world-of-warcraft/game/tests/t_smoke.c](../games/world-of-warcraft/game/tests/t_smoke.c) exercises real `Wow_Clamp` / `Wow_Read32` / `Wow_ReadFloat`.

```
$ make test-wow-engine
=== running tests: * ===
  wow_smoke.clamp                                      PASS
  wow_smoke.read32_little_endian                       PASS
  wow_smoke.read_float_roundtrip                       PASS
=== 5/5 assertions passed in 3 test(s) ===
```

Production `make game-wow` (no `BZ_TESTS`) still builds with the test file present — the
guarded body compiles to nothing, so the shipped module carries no test code.

## Migration status & what fits in-engine

`+test` now also brings up the real game module when no map is given
([common/main.c](../common/main.c) calls `SV_InitGameProgs`), so in-engine tests get the
real `gi` + `globals` and can spawn entities / register real models from the mounted
archives (`test-wow-engine` passes `-data`).

Migrated in-engine (real module, no mocks):

- [t_smoke.c](../games/world-of-warcraft/game/tests/t_smoke.c) — pure `Wow_Clamp`/`Wow_Read32`/`Wow_ReadFloat`.
- [t_appearance.c](../games/world-of-warcraft/game/tests/t_appearance.c) — appearance/equipment pack/unpack.
- [t_combat.c](../games/world-of-warcraft/game/tests/t_combat.c) — AI attack timing, damage, death state and death-frame hold, against the real player model. Replaced the old `test_wow_combat.c` (which hand-reimplemented `Wow_SetEntityMove*`/`Wow_AdvanceEntityFrame`/`Wow_SpawnCorpse`).
- WC3: [t_smoke.c](../games/warcraft-3/game/tests/t_smoke.c) — `compress_stat`, `G_RegionContains`.

Deliberately kept as standalone harness tests (do **not** fit in-engine):

- `test_wow_abilities.c`, `test_wow_entities.c`, `test_wow_game.c` — these define stub
  `G_RegisterModel`/`G_GetAnimation` and a mock `gi` whose call-counters (`ApplyLobbySettings`,
  `unicast`, `configstring`) are the actual assertions. Compiled into the module those stubs
  would collide with the real symbols, the mock observations vanish against real `gi`, and some
  paths need spell/missile models the install lacks (`impact models — fire=0 frost=0`).
- `test_wow_appearance.c` (entity-state delta) — links `common/msg.c`+`common/net.c`; the
  game dylib can't reference `MSG_*` without `-Wl,-undefined,dynamic_lookup` (forbidden).

**Guideline:** move a test in-engine when it exercises real game logic that works with real
(or absent-but-graceful) assets. Keep it standalone when its correctness depends on *mocking*
module internals or on protocol/serialization helpers the game module doesn't link.


## Developer workflow

Add a test:

1. Create `games/<game>/game/tests/t_<area>.c`.
2. Write `#ifdef BZ_TESTS` … `TEST(area.case) { ... }` … `#endif`.
3. `make test-<game>` (or run a subset: `+test 'area.*'`).

Run a single case while debugging:

```sh
build/bin/openwow -data <data> +dedicated 1 +test 'wow_combat.pain_interrupts_attack'
```

## Options considered

| Option | Verdict |
|---|---|
| **A. Tests compiled into the game module, self-registered, run under headless dedicated server** (this proposal) | **Recommended.** Zero Makefile wiring, real `gi`, real game code, one `+test` surface, production-excluded via `-DBZ_TESTS`. |
| B. Each test a separate `.so` loaded via `dlopen` at `+test <path>` | More moving parts (extra link targets, symbol resolution against the game lib, RPATH). Useful only for *out-of-tree* tests. Keep as an optional `+testmodule <path>` later; not the default. |
| C. Keep the standalone harness binaries | Status quo. Rejected: mocks drift from real code and every test needs Makefile + `main()` edits. |

Constructor-based self-registration (A) gives the same "just drop a file" ergonomics as
dlopen modules (B) without a second binary boundary, because the game is already a
dynamically loaded module.

## Migration plan

1. Add `common/test.h` + `common/test.c` (registry, macros, runner) and register the
   `test` command + `Com_Quit` exit path in core.
2. Relax dedicated boot to accept `+test` without `+map`.
3. Add `-DBZ_TESTS` game-lib variant + `openwow-tests` / `opensc2-tests` /
   `openwarcraft3-tests` targets; wire `test-<game>` to boot and run.
4. Port tests incrementally: move assertions from `tests/test_wow_*.c` into
   `games/world-of-warcraft/game/tests/t_*.c`, deleting the mirrored global declarations
   and stub function bodies as each real function becomes directly callable.
5. Retire `test_harness.c` and the per-test `test_schema` recipes once their assertions
   are ported. Keep `test_framework.h` only until the last consumer is migrated, then
   remove.
6. Fixture MPQs (`build/tests/*.mpq`) stay as-is; tests point the server `-data` at them.

## Open questions

- **Determinism of real `gi`.** Real `ReadFile`/SLK loading pulls from fixture MPQs; confirm
  the tiny fixtures cover every table current mocks fabricate (`UnitBalance`, `UnitData`,
  `UnitUI`). Where a fixture is impractical, expose a `gi.CvarString`-driven override rather
  than re-mocking.
- **Per-test isolation.** Decide whether the runner re-inits game state between tests
  (`ge->Shutdown(); ge->Init();`) or tests are responsible for spawning/despawning. Start
  with explicit spawn/teardown; add a `TEST_RESET` hook if cross-test bleed appears.
- **Windows constructors.** `__attribute__((constructor))` works under MinGW/GCC; verify on
  the MSVC path if that ever ships, else fall back to a generated registration list.
- **CI headlessness.** `+dedicated 1` already runs with no GL/SDL window, so this fits the
  headless CI constraint in [CONTRIBUTING.md](../CONTRIBUTING.md).
