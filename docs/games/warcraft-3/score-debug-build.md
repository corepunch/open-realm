# Warcraft III score and game-result tracing build switch

The game module's `G_GameResultDebug` helper emits `WC3_RESULT` records for
result/score-screen transitions, score calculations, hero events, unit creation,
removal/death/transfer, `ShowUnit` visibility, and food accounting (where those
trace sites are present). To keep these investigation traces off in normal
builds, compile with `WC3_DEBUG_SCORE=1` and explicitly enable the existing
runtime cvar when running:

```sh
make build -j4 FFMPEG=1 WC3_DEBUG_SCORE=1
./build/bin/openwarcraft3 -data 'data/Warcraft III/' +set wc3_game_result_debug 1 -tft +map 'Maps\FrozenThrone\Campaign\UndeadX07c.w3x'
```

The build switch is disabled by default. `wc3_game_result_debug 1` cannot
activate these records when the module was built without `WC3_DEBUG_SCORE`;
when built with the flag, the cvar still defaults to off. Both switches are
required to emit the `WC3_RESULT` trace. This affects diagnostics only; score
accounting and gameplay do not depend on the switch.

Other independent diagnostics (`wc3_dialog_debug`, `ui_window_debug`, etc.)
retain their own controls. This switch gates the common result/score logger,
not unrelated warnings or output sent directly to stderr by other systems.

After changing `WC3_DEBUG_SCORE`, rebuild the affected objects: a plain
incremental make may not notice changes to compiler flags if its object rules
track only timestamps. A clean build (or removal of the previous game-module
objects) avoids accidentally reusing instrumentation from the earlier mode.
