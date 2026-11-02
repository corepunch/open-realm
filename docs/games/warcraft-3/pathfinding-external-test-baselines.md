# Small external regression baselines

A self-contained C regression can run against a frozen, already compiled game
module before the implementation changes. The existing shared `Test_Register`
registry accepts tests from a small ELF library loaded with `LD_PRELOAD`. This
avoids rebuilding an unchanged unity module just to record the failing baseline.
The final tests still live in the repository and compile normally into the game
module; final gates run without an external library.

This is a diagnostic workflow, not an engine-framework change or a substitute
for the normal production and test builds. A plugin must contain test code and
fixture setup only. It must not define, replace or intercept game behavior.
Verify that `nm -D --defined-only plugin.so` exposes no game implementation.
Use exported test setup helpers and public game APIs. A private static helper
from the unity module cannot be called from another ELF library. The prototype
uses the existing default test world rather than copying an inaccessible routing
helper or faking its result.

## Freeze the ABI and loaded module

Use headers from the selected baseline snapshot, not the current edited tree.
Record the exact module and test executable paths and SHA256 hashes. Preserve
all transitive header inputs with `gcc -MM`, including conditional-build flags.
Do not rebuild or replace the selected module while a reader is running.

Direct access to `edict_t`, `level` or other structs requires ABI guards. Extract
sizes and named byte offsets from the selected module's DWARF and generate C
`_Static_assert(sizeof(...))` and `offsetof(...)` checks against the snapshot
headers. Also preserve field types and signatures through the complete header
hash inventory. Reject mismatches; padding fields at the end are not permission
to combine headers from a different implementation.

The Captain369 prototype checks431 sizes/offsets across nine relevant types.
Its constructor resolves `G_BotCaptainVsPlayer` with `dladdr` and requires the
actual loaded library's canonical path to equal the selected baseline path. The
launching process hashes that file before and after the run. Merely printing an
expected hash without this path and file check would not establish provenance.

## Prototype commands and results

The frozen prototype is under the external report directory
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-03.4.7.3/payoff358`.
`selection369-dynamic-guarded-metadata.json` pins51 source/header inputs, the
module, executable and plugin. `selection369-dynamic-header-snapshot.tar.gz`
preserves those inputs. `selection369-dynamic-abi.json` is the original DWARF
layout; `selection369-dynamic-abi-guard.c` contains the431 compile-time guards
and loaded-library check. `selection369-dynamic-diagnostic.c` contains only the
two tests and their setup helper.

The prototype was compiled from the selected shared Save208 baseline before any
selection-owner implementation. The following commands show that exact local
launch shape; substitute a frozen snapshot root for future work.

```sh
baseline_root=/run/media/lofcz/ssd_external/GitHub/open-realm
report_root=/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-03.4.7.3/payoff358
cd "$baseline_root"
gcc -shared -fPIC -DBZ_TESTS -DWC3 -DBZ_GAME='"warcraft-3"' \
  -I. -Ishared -Ishared/types -Igames/warcraft-3 \
  -Igames/warcraft-3/common -Igames/warcraft-3/game \
  -Igames/warcraft-3/game/skills -Igames/warcraft-3/game/api \
  "$report_root/selection369-dynamic-diagnostic.c" \
  "$report_root/selection369-dynamic-abi-guard.c" -ldl \
  -o "$report_root/selection369-dynamic-guarded.so"
nm -D --defined-only "$report_root/selection369-dynamic-guarded.so"
LD_PRELOAD="$report_root/selection369-dynamic-guarded.so" \
LD_LIBRARY_PATH="/GitHub/wc3-analysis/native-sdl2:$baseline_root/build/lib" \
TEST_JUNIT="$report_root/selection369-dynamic-guarded.xml" \
  "$baseline_root/build/bin/openwarcraft3-tests" \
  -data "$baseline_root/build/tests" +dedicated 1 \
  +test 'wc3_bot.captain_dynamic369*'
```

The guard reports loaded module SHA256
`99bdbc7aa2d27901cd2326486f3ce151e277d4453eee18acb71cd267106b4f98`.
The plugin SHA256 is
`0fd17d8db7910643fd4d457f2bd82fef2cff56b33d54105cbb0570aab7c3fb5d`.
The diagnostic runs two tests and passes34/42 assertions, failing the same eight
selection behaviors as the ordinary compiled Captain369 baseline. Its coordinates
use the default test-world bounds, so this diagnostic does not replace the
registered production fixture or its retail captures.

Use fresh output paths, require nonzero test counts and inspect JUnit plus the
assertion totals. After implementation, run the normal compiled tests against
both editions and remove `LD_PRELOAD` from the final verifier environment.
