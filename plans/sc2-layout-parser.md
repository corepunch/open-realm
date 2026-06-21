# SC2 XML Layout Parser — Current State & Bug Fixes

## Status

The SC2 `.SC2Layout` XML parser (`sc2_layout.c`, ~920 lines) is built, compiles,
and loads real game HUD layout files. A separate `libui-sc2` UI library exists.
A custom test suite (`test_sc2_layout.c`, 14 test cases) is written and runs.

Test run result: **185/236 assertions pass, 51 fail** (map tests are unrelated
regressions from earlier work, not layout-related).

---

## Architecture

- `games/starcraft-2/ui/sc2_layout.c` — XML parser, template inheritance, constant registry,
  frame tree builder, flattening into `uiBaseFrame_t[]`
- `games/starcraft-2/ui/sc2_layout.h` — public API and types
- `games/starcraft-2/ui/ui_main.c` — SC2 UI library entry (`UI_GetAPI()`)
- `games/starcraft-2/tests/test_sc2_layout.c` — 14 test cases with `.SC2Layout` fixtures
- `games/starcraft-2/tests/resources-src/UI/Layout/` — fixture `.SC2Layout` files
- Build: `opensc2` links `-lui-sc2`; test-sc2 compiles layout tests manually

### Key design decisions

- Uses libxml2 (already linked for `sc2_map.c`)
- Template lookup uses basename fallback (`Dir/Name` → `Name`)
- Template inheritance is deep clone of children + property overlay
- `SC2_MAX_FRAMES` = 4096, `SC2_MAX_TEMPLATES` = 1024, `SC2_MAX_CHILDREN` = 64 per frame
- Test uses `FS_ReadFileQ3` wrapper to match `uiImport_t.FS_ReadFile` signature
- Test MPQ packed manually with `mpqtool pack` using forward-slash paths

---

## Bugs Found (from test run)

### Bug 1: Double-hash `##` constant resolution

**Symptom:** `test_layout_constants_parsed` fails — `SC2_LayoutResolveConstant("##TestColorRed")`
returns NULL instead of `"255,0,0"`.

**Root cause:** `SC2_LayoutResolveConstant()` only strips one `#`:

```c
if (!name || name[0] != '#') return name;
name++; /* skip '#' */
```

But SC2 constants are referenced with `##` prefix. After stripping one `#`, the
name is `#TestColorRed`, which doesn't match the stored `TestColorRed`.

**Fix:** Strip all leading `#` characters, not just one:

```c
while (*name == '#') name++;
```

### Bug 2: Template inheritance — same-file templates don't inherit properly

**Symptom:** `test_layout_template_inheritance` fails — `button->num_anchors == 1` but expected 3.
`test_layout_gameui_full_parse` fails — `included->has_width` is false, `button->has_width` is false.

**Root cause:** Templates within the same file are parsed sequentially. When
`TestButtonTemplate` declares `template="TestTemplates/TestBaseTemplate"`, the
`SC2_ResolveTemplate` is called during the parse of `TestButtonTemplate`. But
`TestBaseTemplate` exists in the same file and may not have been fully parsed yet
(anchors, textures not yet populated when the template reference is resolved).

**Diagnosis needed:** Read the frame parsing code to confirm ordering. The fix is
likely: do a second pass after all frames are parsed to resolve template
references, or ensure templates are parsed bottom-up within a file.

### Bug 3: Constant resolution in anchor offsets (`#TestGap`)

**Symptom:** `test_layout_constant_offset` fails — `button->anchors[0].offset == 0` but expected 4.

**Root cause:** `SC2_ParseAnchor` reads offset via `xmlGetAttrInt(node, "offset", ...)` which
does `atoi()` directly. It never calls `SC2_LayoutResolveConstant()` on the raw
attribute string before parsing.

**Fix:** In `SC2_ParseAnchor`, read the offset as a string first, run it through
`SC2_LayoutResolveConstant()`, then `atoi()` the result:

```c
LPCSTR offset_str = SC2_XmlGetProp(node, "offset");
if (offset_str) {
    LPCSTR resolved = SC2_LayoutResolveConstant(offset_str);
    a->offset = (int16_t)atoi(resolved);
    SC2_XmlFree(offset_str);
}
```

Same fix needed for `<Width val="..."/>` and `<Height val="..."/>` children that
may use `#constant` values.

### Bug 4: GameUI template inheritance for IncludedPanel

**Symptom:** `test_layout_gameui_full_parse` — `IncludedPanel` child frames don't inherit
width/height from their template.

**Root cause:** Related to Bug 2 — `IncludedFrame` is defined in `TestIncluded.SC2Layout`
which is included via `<Include>`. The include is processed recursively, so
`IncludedFrame` should be parsed before `GameUI`. But the `template="TestIncluded/IncludedFrame"`
reference on `IncludedPanel` uses a path with a directory prefix. The basename
lookup should find `IncludedFrame`, but the template may not have its properties
yet (same ordering issue as Bug 2).

**Verify:** Add debug output to `SC2_ResolveTemplatePath` to confirm it finds the
template, and that the template has `has_width` set at resolution time.

---

## Test Failure Summary

| Test | Status | Root Cause |
|------|--------|------------|
| test_layout_constants_parsed | FAIL | Bug 1 (## stripping) |
| test_layout_unknown_constant_returns_null | PASS | — |
| test_layout_include_resolves | PASS | — |
| test_layout_template_inheritance | FAIL | Bug 2 (same-file ordering) |
| test_layout_template_children | PASS | — |
| test_layout_gameui_full_parse | FAIL | Bug 2 + Bug 4 |
| test_layout_nested_children | PASS | — |
| test_layout_anchors_parsed | PASS | — |
| test_layout_textures_parsed | PASS | — |
| test_layout_flatten_to_frames | FAIL | Likely Bug 2 (inherited frames missing) |
| test_layout_multiple_parses | FAIL | Bug 1 (constants not resolved) |
| test_layout_reinit_clears | PASS | — |
| test_layout_constant_offset | FAIL | Bug 3 (offset not resolved) |
| test_layout_texture_layers | PASS | — |

---

## Fix Plan (in order)

1. **Fix Bug 1** — `SC2_LayoutResolveConstant`: strip all leading `#` chars
2. **Fix Bug 3** — `SC2_ParseAnchor`: resolve `#constant` offsets before atoi
3. **Fix Bug 3 for Width/Height** — `SC2_ParseFrameChildren`: resolve `#constant` in
   `<Width val="..."/>` and `<Height val="..."/>` before parsing as float
4. **Fix Bug 2** — Template inheritance ordering: add a second resolution pass
   after all frames in a file are parsed, or defer template resolution until
   flatten time
5. **Fix Bug 4** — Verify GameUI template path lookup finds templates from included files
6. **Re-run tests** — `build/bin/test_sc2`

---

## MPQ Test Fixture

Packed via:
```
build/bin/mpqtool create build/tests/test-sc2.SC2Maps \
    Maps/Test/MarSara.SC2Map tests/resources-src/Maps/Test/MarSara.SC2Map \
    Maps/Test/Tiny.SC2Map tests/resources-src/Maps/Test/Tiny.SC2Map \
    UI/Layout/TestConstants.SC2Layout tests/resources-src/UI/Layout/TestConstants.SC2Layout \
    UI/Layout/TestIncluded.SC2Layout tests/resources-src/UI/Layout/TestIncluded.SC2Layout \
    UI/Layout/TestTemplates.SC2Layout tests/resources-src/UI/Layout/TestTemplates.SC2Layout \
    UI/Layout/TestGameUI.SC2Layout tests/resources-src/UI/Layout/TestGameUI.SC2Layout
```

---

## Files To Edit

| File | Change |
|------|--------|
| `games/starcraft-2/ui/sc2_layout.c:238` | Strip all leading `#` in `SC2_LayoutResolveConstant` |
| `games/starcraft-2/ui/sc2_layout.c:398` | Resolve `#constant` in anchor offset before `atoi` |
| `games/starcraft-2/ui/sc2_layout.c` (frame parse) | Resolve `#constant` in Width/Height val before parse |
| `games/starcraft-2/ui/sc2_layout.c` (frame parse) | Resolve `#constant` in Color, Visible, Alpha val attributes |
| `games/starcraft-2/ui/sc2_layout.c` (template resolution) | Defer or second-pass template resolution |
| `games/starcraft-2/tests/test_sc2_layout.c` | Update assertions if needed after fixes |
