# Warcraft III score and game-result tracing build switch

The game module's `BZ_WC3_SCORE_LOG(...)` macro uses `G_GameResultDebug` to emit
`WC3_RESULT` records for result/score-screen transitions, score calculations,
and bounded score and result-stage summaries. To keep these investigation traces
off in normal builds, compile with `WC3_DEBUG_SCORE=1` and enable the existing
runtime cvar when running:

```sh
make build -j4 FFMPEG=1 WC3_DEBUG_SCORE=1
./build/bin/openwarcraft3 -data 'data/Warcraft III/' +set wc3_game_result_debug 1 -tft +map 'Maps\FrozenThrone\Campaign\UndeadX07c.w3x'
```

The build switch is disabled by default. Every score/result trace call uses
`BZ_WC3_SCORE_LOG(...)`, which expands to `((void)0)` in normal builds; its format
and arguments are not evaluated. Instrumented builds route those calls through
the helper, which checks the runtime cvar before printing. Investigation-only
per-unit lifecycle, per-trigger, per-score-row, first-frame survivor, and
obsolete tech-comparison traces have been removed; gameplay score tracking is
always enabled.
`wc3_game_result_debug 1` cannot activate these records when the module was
built without `WC3_DEBUG_SCORE`; when built with the flag, the cvar still
defaults to off. Both switches are
required to emit the `WC3_RESULT` trace. This affects diagnostics only; score
accounting and gameplay do not depend on the switch.

Other independent diagnostics (`wc3_dialog_debug`, `ui_window_debug`, etc.)
retain their own controls. This switch gates the common result/score logger,
not unrelated warnings or output sent directly to stderr by other systems.

After changing `WC3_DEBUG_SCORE`, rebuild the affected objects: a plain
incremental make may not notice changes to compiler flags if its object rules
track only timestamps. A clean build (or removal of the previous game-module
objects) avoids accidentally reusing instrumentation from the earlier mode.

`wc3_score.disabled_score_log_skips_argument_evaluation` verifies that macro
arguments are skipped in a normal build and evaluated in an instrumented build.
`wc3_score.score_log_runtime_cvar_controls_output` captures actual stderr
output and verifies that only the instrumented build with the runtime cvar set
prints the tagged record.
