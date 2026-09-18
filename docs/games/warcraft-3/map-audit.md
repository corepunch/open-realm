# Warcraft III Campaign Map Audit

`tools/wc3_map_audit.py` runs every retail RoC and TFT campaign map for a
bounded number of headless server frames, and can smoke-run a loose custom map
under the data tree with `--loose-map`. It retains raw logs and produces a
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

Custom-map playability for DotA is tracked separately in
[#431](https://github.com/corepunch/open-realm/issues/431).

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
--loose-map PATH    audit a disk-resident .w3m/.w3x under --data (repeatable)
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

## Custom Maps

Pass `--loose-map` for a disk-resident scenario under the data tree (typically
`data/Warcraft III/Maps/*.w3x`). The auditor still isolates `XDG_DATA_HOME`,
assigns a unique `game_port`, enables `com_fast_forward` / `vid_hidden` /
`+dedicated 1`, and adds `-tft` for `.w3x` (RoC for `.w3m`). Crash and timeout
outcomes are recorded the same way as campaign maps.

```sh
python3 tools/wc3_map_audit.py \
  --data 'data/Warcraft III' \
  --binary build/bin/openwarcraft3 \
  --mpqtool build/bin/mpqtool \
  --jobs 1 --frames 10 --timeout 60 \
  --loose-map 'data/Warcraft III/Maps/DotA v6.83dAI PMV 1.42 EN.w3x'
```

Equivalent Make form:

```sh
make audit-wc3-maps WC3_AUDIT_ARGS="--jobs 1 --frames 10 --timeout 60 --loose-map 'data/Warcraft III/Maps/DotA v6.83dAI PMV 1.42 EN.w3x'"
```

Without `--loose-map`, the enumerator only walks retail campaign members inside
`War3.mpq` / `War3x.mpq`. See [DotA Custom-Map Playability](dota-map-playability.md).
