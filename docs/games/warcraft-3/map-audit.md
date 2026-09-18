# Warcraft III Campaign Map Audit

`tools/wc3_map_audit.py` runs every retail RoC and TFT campaign map for a
bounded number of headless server frames. It retains raw logs and produces a
JSON report plus a GitHub-ready Markdown matrix containing the human-readable
map name, filename, result, and errors observed for every map.

This is a smoke audit, not a completion test. Reaching the frame limit does not
show that objectives progress, the player can survive, cinematics render, or a
mission can be won.

## Prerequisites

Build the engine and MPQ diagnostic tool, and ensure the retail installation is
available at `data/Warcraft III`:

```sh
make build/bin/openwarcraft3 build/bin/mpqtool
```

The Make target builds these dependencies automatically.

## Run All Campaign Maps

The canonical audit uses 600 frames (60 simulated seconds at the fixed 10 Hz
server loop), four isolated workers, a 120-second per-map wall timeout, and a
serial confirmation run for every first-pass crash:

```sh
make audit-wc3-maps
```

Outputs are written to:

- `build/wc3-map-audit/report.json`: complete machine-readable results;
- `build/wc3-map-audit/report.md`: issue-ready summary and per-map tables; and
- `build/wc3-map-audit/logs/`: complete first-pass and serial-rerun logs.

To replace the body of the tracking issue with a fresh report:

```sh
gh issue edit 418 --body-file build/wc3-map-audit/report.md
```

Hero walk / save / load on the same campaign enumerator is a separate local
diagnostic: `make audit-wc3-hero-saveload`. See
[Save/Load](save-load.md#hero-walk--save--load).

## Focused and Longer Runs

Pass runner arguments through `WC3_AUDIT_ARGS`:

```sh
make audit-wc3-maps WC3_AUDIT_ARGS='--map Human05.w3m --frames 18000 --jobs 1'
make audit-wc3-maps WC3_AUDIT_ARGS='--map "Human*.w3m" --frames 1200 --jobs 2'
```

At 10 Hz, `18000` frames requests 30 simulated minutes. For Human05 that can
exercise timer expiry only if gameplay is able to advance without player
actions. It still cannot prove that the defense is winnable. A deterministic
scenario must separately establish a valid player state and assert timer,
attack-wave, objective, survival, and victory milestones.

Useful direct options include:

```text
--frames N          server-frame budget per map
--timeout N         wall-clock seconds per map
--jobs N            concurrent isolated map processes
--map GLOB          filename or archive-path filter
--limit N           audit only the first N filtered maps
--rerun-crashes     rerun first-pass crashes serially
--fail-on-crash     return nonzero after reports have been written
--output-dir PATH   report/log destination
```

Each worker receives a temporary `XDG_DATA_HOME` and a unique `game_port`.
This prevents persistent campaign state and socket collisions from leaking
between maps. Four workers is intentionally conservative because large maps can
consume significant memory.

## Run the Tool Tests

Parser, diagnostic-compaction, and report tests require no retail data:

```sh
make test-wc3-map-audit
# equivalent:
python3 tests/test_wc3_map_audit.py
```

For an integration check against retail data without sweeping all maps:

```sh
make audit-wc3-maps WC3_AUDIT_ARGS='--map Human05.w3m --frames 10 --jobs 1'
```

## Interpretation

The report preserves exact native names, AI script paths, unit rawcodes, event
counts, and full raw logs. High-volume unit/resource messages are compacted
into counted families so one map remains one readable table row.

`completed` means only that the process reached `com_frame_limit` with exit
code zero. `SIGSEGV`, `timeout`, and `auditor-error` are process-level failures.
Warnings and runtime errors remain listed even when the process reaches its
frame limit. Visual correctness requires a rendered pass or human inspection;
mission completion requires scripted milestones or human play.
