# Parity harness

Tools for keeping the reimplementation visually faithful to the original
Warcraft III, and for catching rendering regressions.

## 1. `shot.sh` — live window capture (macOS)

Launches a client, captures its GL window to a PNG by window-id (reliable even
when the window is occluded), then kills it.

```sh
tools/parity/shot.sh --app ours   --screen menu_main -o /tmp/ours.png
tools/parity/shot.sh --app legacy                    -o /tmp/legacy.png
tools/parity/shot.sh --app both   -o /tmp/parity.png   # ours + Legacy, side by side*
```

`--app both` writes `*-ours.png` and `*-legacy.png`; if ImageMagick's `montage`
is installed it also writes `*-sidebyside.png`. Options: `--data <dir>`,
`--screen <menu_cmd>`, `--delay <sec>`, `--keep`.

The Legacy client is the original at
`/Applications/Warcraft III (Legacy)/Warcraft III.app` (v1.29.2) — the parity
reference.

## 2. `render_golden.sh` — golden-image regression test

Renders each model in `golden_manifest.txt` to a **deterministic** PNG via
`mdxtool -o` (fixed frame + seeded particle RNG → byte-stable output) and
compares it to a committed reference in `golden/` with `imgdiff`. Fails if any
render drifts beyond the mean-pixel-difference threshold.

```sh
make test-render-golden        # compare against golden/ (exit non-zero on drift)
make update-render-golden      # regenerate golden/ after an intentional change
# or directly:
tools/parity/render_golden.sh [--update] [--threshold 2.0] [--data <dir>]
```

This requires a display/GL (mdxtool opens a window), so it is **opt-in** and not
part of `make test` (CI is headless).

### Manifest format

`name | mpq | model | extra mdxtool args` — one render per line. Pick models
self-contained in one archive that exercise distinct renderer paths (flipbook
water, team color, particles, geometry). Glue scenes need `--use-model-camera`;
units use the default fitted orbit camera.

When a comparison fails, the offending render and an amplified diff are written
to `tools/parity/_last_fail_<name>.png` / `_last_fail_<name>.diff.png` for
inspection.

## Pieces

- `mdxtool -o <png> [--frame <ms>] [--seed <n>]` — deterministic clean render to
  PNG (in `tools/mdxtool.c`).
- `imgdiff a b [--threshold m] [--pixel-tol t] [--diff out.png]` — image compare
  (in `tools/imgdiff.c`; standalone, stb-only).
- `golden/` — committed reference PNGs (regenerate with `--update`).
