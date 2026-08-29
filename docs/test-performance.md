# Test Performance

## Execution Contract

The aggregate `test` target runs independent standalone, engine, and game-suite targets. Each `test_schema` target now has two stages: a persistent binary target and a test execution target. The binary depends on its compiled source list, so source edits rebuild it while repeated test runs execute without recompiling.

The aggregate target runs the suites concurrently through recursive Make. `TEST_JOBS` controls the concurrency and defaults to 16; this was fastest on the local 8-core macOS machine in the measurements below.

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

## Diagnostic Workflow

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
