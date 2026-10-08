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
