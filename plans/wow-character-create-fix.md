# Fix WoW Character Create Menu: Lua Errors + Class Panel

## Goal
Fix 3 issues in the WoW character create menu:
1. `RealmTypeSort:OnLoad` Lua error — `getglobal(this:GetName().."Arrow")` returns nil
2. `CharacterCreateEnumerateClasses:162` Lua error — `getglobal("CharacterCreateClassButton"..i)` returns nil
3. Class header shows "Race" instead of the class name

## Root Cause Analysis

### Issue 1: `RealmTypeSort:OnLoad` — Arrow nil

The `RealmSortButtonTemplate` (RealmList.xml:118) defines `<NormalTexture name="$parentArrow">`
as a **direct child** of the Button — NOT inside `<Layers>` or `<Frames>`.

`UIWow_XmlParseChildren` (ui_xml.c:935) only processes `<Layers>`, `<Frames>`, and `<ScrollChild>`
child nodes. It **skips** direct children like `<NormalTexture>`, `<NormalText>`, `<HighlightTexture>`, etc.

Result: The `$parentArrow` NormalTexture is never parsed as an XML element, never added to
`wow_xml.elems`, never cloned by `UIWow_XmlCloneTemplateChildren`, and never published as a Lua
global. The OnLoad script `getglobal(this:GetName().."Arrow")` fails because `RealmTypeSortArrow`
was never created.

The concrete `UIWow_XmlPublishFrame` (ui_xml.c:626-634) publishes synthetic children for buttons
like `ButtonNameNormalTexture`, but NOT `ButtonNameArrow` (the template-resolved name from `$parentArrow`).

### Issue 2: `CharacterCreateEnumerateClasses:162` — Class button nil

The class buttons (CharacterCreate.xml:914-979) are inside `<Frames>` and SHOULD be parsed.
Two possible causes:

**A) XML element overflow**: Template child cloning multiplies elements significantly.
Each concrete element inheriting a template creates clones of ALL the template's direct children.
If the total exceeds `WOW_XML_MAX_ELEMS` (2048), `UIWow_XmlPushElem` returns -1 and elements
are silently skipped (ui_xml.c:223, logged at line 1065). The class buttons are late in
CharacterCreate.xml — if the limit is hit before them, they're never created.

**B) Inline children not parsed** (same root cause as Issue 1): If `CharacterCreateClassButtonTemplate`
has inline NormalTexture/HighlightTexture children that aren't parsed, the template clone children
list is empty, and the published synthetic names don't match what the Lua code expects.

### Issue 3: Class header shows "Race"

Cascading failure from Issue 2. The call chain in `CharacterCreate_OnShow`:
```
line 78: SetCharacterRace(GetSelectedRace())
  → line 241 (compat): CharacterCreateEnumerateClasses(GetClassesForRace())
    → line 162 (compat): getglobal("CharacterCreateClassButton"..i):Hide()  ← CRASHES
```
The error propagates up through `SetCharacterRace` back to `CharacterCreate_OnShow`,
aborting lines 80-88. `SetCharacterClass(1)` at line 82 never executes, so the
label keeps its XML default text "RACE".

## Plan

### Phase 1: Diagnostics

**Step 1**: Add `fprintf(stderr, ...)` to `UIWow_XmlPushElem` (ui_xml.c:221) to track
the element count at each push and detect overflow. Log when count crosses thresholds
(1024, 1536, 1900) and when the limit is hit.

**Step 2**: Add `fprintf(stderr, ...)` in `UIWow_XMLLoadGlueFromToc` (ui_xml.c:1222) to
log the final `wow_xml.count` after all files are loaded.

**Step 3**: Add a diagnostic Lua test after GlueXML loads: iterate globals and log any
starting with `CharacterCreate` or `RealmTypeSort` to confirm which are published.

**Step 4**: Build and run:
```
build/bin/openwarcraft3 -data "data/Warcraft III" +r_module stdout +com_frame_limit 1 +menu_main
```
Capture stderr for diagnostic output.

### Phase 2: Fix (based on diagnostic results)

**Fix A — `UIWow_XmlParseChildren` inline children** (if Issue 1 root cause confirmed):

In `UIWow_XmlParseChildren` (ui_xml.c:935), add handling for direct children that are
WoW "inline" types: `NormalTexture`, `HighlightTexture`, `PushedTexture`, `DisabledTexture`,
`NormalText`, `DisabledText`, `HighlightText`. These need to be parsed as elements so they
appear in `wow_xml.elems` with the correct parent, enabling template cloning to find and
clone them with `$parent` name substitution.

Approach: After the existing `<Frames>/<ScrollChild>` block, add an `else` branch that
calls `UIWow_XmlParseNode(c, parent, layer)` for unrecognized child element types.
`UIWow_XmlParseNode` already handles all these types (lines 1047-1048) and returns early
for unrecognized ones (line 1049).

```c
static void UIWow_XmlParseChildren(xmlNodePtr node, int parent) {
    xmlNodePtr c;
    for (c = node->children; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE) continue;
        if (!xmlStrcasecmp(c->name, BAD_CAST "Layers")) {
            xmlNodePtr l; for (l = c->children; l; l = l->next)
                if (l->type == XML_ELEMENT_NODE && !xmlStrcasecmp(l->name, BAD_CAST "Layer"))
                    UIWow_XmlParseLayer(l, parent);
            continue;
        }
        if (!xmlStrcasecmp(c->name, BAD_CAST "Frames") || !xmlStrcasecmp(c->name, BAD_CAST "ScrollChild")) {
            xmlNodePtr f; for (f = c->children; f; f = f->next)
                UIWow_XmlParseNode(f, parent, WOW_XML_LAYER_ARTWORK);
            continue;
        }
        // NEW: Parse inline WoW children (NormalTexture, NormalText, HighlightTexture, etc.)
        UIWow_XmlParseNode(c, parent, WOW_XML_LAYER_ARTWORK);
    }
}
```

This is safe because `UIWow_XmlParseNode` returns early for unrecognized tag names (line 1049)
and for non-element nodes (line 1035). Known inline types like `<Size>`, `<Anchors>`, `<Anchor>`,
`<Offset>`, `<Backdrop>`, `<Scripts>`, `<OnLoad>` are NOT recognized element types in
`UIWow_XmlParseNode` (they fall through to the `return` at line 1049), so they're safely ignored.

**Fix B — Element limit** (if diagnostics show overflow at ~2048):

Increase `WOW_XML_MAX_ELEMS` from 2048 to 4096 in ui_xml.c:112.

**Fix C — Class header text** (auto-resolves once Issue 2 is fixed):

No code change needed. Once `CharacterCreateEnumerateClasses` no longer crashes, the
`SetCharacterClass(1)` at line 82 in `CharacterCreate_OnShow` executes and sets the label
to the correct class name.

### Phase 3: Verification

1. Build and run: `make run-ui-text UI_CMD=menu_character_create`
2. Verify no Lua errors in output
3. Verify class panel shows a class name (e.g., "Warrior"), not "Race"
4. Run the WoW UI test suite: `make -C build test` (if WoW UI tests exist)
5. Run `mdxtool --info` on a class icon to confirm textures load

### Files to Modify
- `games/world-of-warcraft/ui/ui_xml.c` — diagnostics + Fix A (`UIWow_XmlParseChildren`) and/or Fix B (`WOW_XML_MAX_ELEMS`)

### Key Code References
- `ui_xml.c:112` — `WOW_XML_MAX_ELEMS = 2048`
- `ui_xml.c:221-237` — `UIWow_XmlPushElem` (element creation, limit check at line 223)
- `ui_xml.c:532` — `UIWow_LuaGetGlobalCompat` (getglobal implementation)
- `ui_xml.c:613-636` — `UIWow_XmlPublishFrame` (publishes element as Lua global + synthetic children for buttons)
- `ui_xml.c:935-948` — `UIWow_XmlParseChildren` (THE BUG: only processes Layers/Frames/ScrollChild)
- `ui_xml.c:955-1031` — `UIWow_XmlCloneTemplateChildren` (clones template children into concrete elements)
- `ui_xml.c:1033-1079` — `UIWow_XmlParseNode` (handles all element types, returns early for unrecognized)
- `ui_xml.c:1192-1223` — `UIWow_XMLLoadGlueFromToc` (loads all GlueXML, fires OnLoad scripts)
