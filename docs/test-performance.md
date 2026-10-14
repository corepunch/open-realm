# Test Performance

## Execution Contract

The aggregate `test` target runs independent standalone, engine, and game-suite targets. Each `test_schema` target now has two stages: a persistent binary target and a test execution target. The binary depends on its compiled source list, so source edits rebuild it while repeated test runs execute without recompiling.

The aggregate target runs the suites concurrently through recursive Make. `TEST_JOBS` controls the concurrency and defaults to 16; this was fastest on the local 8-core macOS machine in the measurements below.

The `test-jass-build` shell recipe is marked recursive with `+` because its script invokes Make. This preserves
GNU Make jobserver descriptors during parallel suite execution and avoids the Linux `jobserver unavailable` warning.

## Benchmark

Measured on `main` at commit `ac44fc12` before the change:

| Run | Tests | Wall time |
| --- | ---: | ---: |
| Initial `make test` including builds | 1,396 | 52.30 s |
| Repeat `make test` with existing build | 1,396 | 28.22 s |

After incremental binaries and parallel suite execution:

| Run | Tests | Wall time |
| --- | ---: | ---: |
| First run after rule migration | 1,396 | 5.13 s |
| Steady-state run, `TEST_JOBS=16` | 1,396 | 4.79 s |
| Plain `make test` using the default | 1,396 | 5.65 s |

The final runs passed all 17 suite summaries. The default run processes about 1,235 tests per 5 seconds, exceeding the 1,000-tests-per-5-seconds target. With `TEST_JOBS=16`, throughput is about 1,457 tests per 5 seconds. Compared with the repeatable pre-change run, default wall time improved by 80%.

## October 2026 Regression

Steady-state `make test` on the same 8-core M1 (4 performance + 4 efficiency cores), measured at historical commits:

| Commit | Date | Tests | Wall time |
| --- | --- | ---: | ---: |
| `b1e69ec2c` | 2026-08-28 | 1,408 | 3.7 s |
| `22abe18d0` | 2026-10-01 | 5,416 | 32.6 s |
| `445891148` | 2026-10-08 | 6,198 | 47.0–57.6 s |
| `445891148` + this change | 2026-10-08 | 6,198 | 17.9–21.7 s |

The WC3 engine suite grew from about 500 to 2,480 tests and ran twice (classic, then TFT) in one serial
recipe, about 19 s per pass. More than half of each pass was `wc3_save`: every round trip writes and rereads a
5.8 MB save, and the footer checksum hashed it one byte at a time.

The fix:

- `test-wc3-engine` runs each data variant as `WC3_TEST_SHARDS` (default 4) round-robin shards through
  `TEST_SHARD=index/count`. Each shard writes `test-wc3-engine-<variant>-<index>.xml`. More shards did not help on
  the M1 because the efficiency cores run the engine tests about half as fast.
- The save footer checksum hashes 64-bit words in four lanes, and save streams use a 1 MB stdio buffer.
  Save version 74.
- `test-core` builds a persistent binary, and the Python audits run inside the parallel phase.

Sharding exposed three tests that depended on HUD state left by earlier tests. Each one now resets or loads the
HUD it asserts against. When adding tests, check order independence with a few shard counts:

```sh
for i in 0 1 2 3 4 5 6; do TEST_SHARD=$i/7 build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test '*'; done
```

`TEST_SLOW_MS=<ms>` prints every test at or above that wall time and the run total:

```sh
TEST_SLOW_MS=50 build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_*' 2>&1 | grep SLOW
```

Remaining cost: each save round trip still writes and rereads about 5.8 MB even for a small test world, and
`G_PoolsReset`/`__bzero` show up in every test's reset. A breakdown of which records make up the file is the next
step. Shrinking it would cut both disk traffic and `wc3_save` time.

## Diagnostic Workflow

### GitHub Actions jobs that never start

A failed workflow can contain only cancelled jobs and no executed steps. Inspect check-run annotations before treating it as
a build or test regression:

```sh
gh run view <run-id> --repo corepunch/open-realm --json conclusion,jobs
gh api repos/corepunch/open-realm/check-runs/<check-run-id>/annotations
```

In [PR #585's CI run](https://github.com/corepunch/open-realm/actions/runs/37364440458), all four ordinary jobs were cancelled
after about 15 minutes with empty step lists. Each reported `The job was not acquired by Runner of type hosted even after
multiple attempts`. This identifies hosted-runner acquisition failure before checkout, compilation, or tests; rerun CI to
obtain code validation. The accompanying Ubuntu migration notices were notices, not the failure. The EOS job was intentionally
skipped by the fork-PR condition in `.github/workflows/c-cpp.yml`. See [CI build environment](../CONTRIBUTING.md#build-and-linking).

### Local suite timing

Run the full suite with timing:

```sh
/usr/bin/time -p make test TEST_JOBS=16
```

On macOS, `xctrace` requires the full Xcode developer directory when Command Line Tools is active:

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xctrace record \
  --template 'Time Profiler' --time-limit 10s --output build/test.trace --launch -- \
  build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test '*'
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xctrace export \
  --input build/test.trace \
  --xpath '/trace-toc/run[@number="1"]/data/table[@schema="time-profile"]' \
  > build/test-time-profile.xml
build/bin/xctraceprof --top 30 build/test-time-profile.xml
```

The final engine-test profile sampled `Test_Run` and showed `__bzero` as the largest leaf symbol, followed by JASS setup and test-specific work. This profile covers the engine-backed WC3 process; aggregate wall timing is required for the full multi-suite target.


## Linux SDL compatibility-layer text-event crash

On a host with`sdl2-compat 2.32.72` and`SDL3 3.4.16`, the unchanged
`client_input.menu_sdl_input_is_exclusive_with_world_presentation` test crashes
inside`SDL_PushEvent` for its`SDL_TEXTINPUT` event, even in isolation. A minimal
SDL-only reproducer also crashes with the dummy video driver and a hidden
window. This is separate from the Way Gate serialization diagnostics printed
earlier by the aggregate suite.

The compatibility layer's`Event2to3` returns NULL for text input, and its
`SDL_PushEvent` forwards that result to SDL3. See the
[upstream conversion](https://github.com/libsdl-org/sdl2-compat/blob/main/src/sdl2_compat.c).
Keep the real queued text-input regression. For verification on this host,
build native SDL2 in an isolated directory and select it for the test process:

```sh
LD_LIBRARY_PATH=/tmp/wc3-sdl2-build:$LD_LIBRARY_PATH make test
```

This does not replace system libraries or change production input handling.

### Route modes335/336: second implementation commit after332

ROUTE-04.1 closes with two game-local fixes: the post-type-change Move timer
recreates its physical point group, and the flying next-step collector respects
no-fly terrain after query restoration. Both failures are preserved before the
fixes; the accepted native inputs are unchanged. Two repeated/control public
matrices cover4122 commits and4081 cache states, with four cold continuations
per matrix. Classic/TFT each pass888 tests /8471883 assertions across full
movement647, unit/morph142, pathfinding97 and blocker-scope2. Production/test
builds pass without warnings. The576-case original cache oracle and strict
fresh route335 corpus report pass. Seven new evidence tests,37 corpus tests
and12 ownership tests pass. All559 older corpus entries remain unchanged.

This is implementation commit2/12 after the332 full checkpoint; it does not
reset that cadence or claim another full repository suite. Logs, JUnit, native
accepted/rejected captures and the patch are archived under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ROUTE-04.1/`.

### Arrival composition337: third implementation commit after332

ROUTE-04.2 closes with unconditional scheduler cancellation on successful
physical arrival. The initial regression failed36 checks; final72 queue/cache
cases, half with cold saves, pass2412 assertions. Both Classic and TFT pass
all648 movement tests /8099511 assertions. The unchanged goal310
verifier separately passes194 tests /829108 assertions per edition plus its
713 native instruction guards,96 public scenes and nine perimeter completions.
Two read-only retail repeats match18600 events and1329 public markers, including
25 cache-preserving arrival unlinks; the original caller matrix passes1536 cases.
Five new Python evidence tests,37 corpus tests and12 ownership tests pass, along
with warning-free production/test builds and the fresh strict corpus entry.
All560 prior corpus entries are unchanged. Initial invalid make target names
and the rejected UINT32_MAX save-fixture counters are retained as diagnostics.

This is implementation commit3/12 after checkpoint332. It does not reset the
full-suite cadence. Logs, JUnit, raw native repeats and the patch are archived
under `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ROUTE-04.2/payoff337/`.

### Acquisition ranking338: fourth implementation commit after332

GROUP-03.2 advances without a new split or closure. The integration fixes
attacker-owned neutral levels, directional victim spell grants, artillery
collateral/veto, normal-player Town ranking and Roots priority/current speed.
The final verifier passes306 tests /32600 assertions in each of Classic and
TFT, including actual owner-change delivery, status removal, spell approach
ownership and cold saves. Original evidence comprises3198 normal events,
3916 hard-Town events with38 candidate words, and476 control-effect events;
each pair matches its uninstrumented public control. Original scalar blocks
check9 health and36 multiplier boundary cases. Failing-first health and Roots
runs are retained, including the separate speed-zero failure after the mobility
fix. Production and test builds pass;14 evidence rejection/validation tests,37 corpus
tests and12 exclusion-ownership tests pass. The fresh strict corpus entry and
staged corpus/ownership inventory pass; all561 older entries are unchanged.

This is implementation commit4/12 after checkpoint332. Full-suite cadence is
unchanged. Eight updated function annotations are saved in Ghidra. Raw captures,
source maps, diagnostic attempts, JUnit and fresh reports are archived under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-03.2/payoff338/verified/`.

### Acquisition control effects339: fifth implementation commit after332

Three failing-first regressions (58 failures /328 assertions) now pass, covering
Sleep admission, natural-sleep scans and five Ensnare/Web binding IDs with
removal/ability/save inverses. The affected suites pass343 tests /32998 assertions
in each of Classic and TFT. Two complete1372-event retail captures agree with
98 public markers from an uninstrumented control. The verifier checks427 original
instructions and fresh class hierarchy extraction. Eight evidence tests,37 corpus
tests and12 ownership tests pass; production and test builds pass. A fresh strict
oracle-group339 report verifies the registered evidence. Existing562 corpus
entries remain unchanged. GROUP-03.2 remains open without another task split.

Cadence is5/12 since full checkpoint332; no full-suite claim for this chunk.
Five annotations are saved in Ghidra. Reports, diagnostic attempts, maps, failing
and passing logs are archived under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/GROUP-03.2/payoff339/`.
