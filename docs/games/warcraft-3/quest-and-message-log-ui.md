# Warcraft III Quest And Message Log UI

## Contract

Warcraft III's Quests journal and single-player Message Log are separate
server-authored HUD dialogs.

- The quest model (`level.quests`) owns quest state. The dialog only renders
  enabled, discovered quests and their objectives.
- `DisplayText*` owns one transient `LAYER_MESSAGE` presentation slot per
  player. A separate bounded `client_s.message_log` retains the text after the
  transient message expires or `ClearTextMessages` clears it.
- Command errors use `UI_ShowTransientText` and do not enter the Message Log.
- `UpperButtonBarQuestsButton` sends the existing `quests` client command.
- `UpperButtonBarChatButton` currently sends `log`, implementing the
  single-player Log role of Warcraft's fourth system button. Multiplayer Chat
  mode selection is separate work.
- Function-key mapping is intentionally outside this subsystem. The F9/F12
  bindings are owned by the input-mapping work and must not be duplicated here.

The dialogs are rendered through stock Warcraft FDF roots:

```text
UI\FrameDef\UI\QuestDialog.fdf
UI\FrameDef\UI\LogDialog.fdf
UI\FrameDef\UI\UpperButtonBar.fdf
```

Generated bindings under `games/warcraft-3/game/generated/` are outputs of
`fdfbindgen`; do not hand-edit them.

## Quest Data Flow

```text
CreateQuest / QuestSet* / QuestCreateItem
        -> level.quests / QUESTITEM state
        -> quests command or ForceQuestDialogUpdate
        -> hud/hud_quests.c
        -> LAYER_QUESTDIALOG
```

`CreateQuest` initializes `enabled = true`; Blizzard's standard `CreateQuestBJ`
sets required/discovered/completed state but does not separately enable the
quest. A quest is visible only when both are true:

```c
quest->enabled && quest->discovered
```

Required and optional quests are rendered into separate authored containers.
`UI_ShowQuests` chooses the first visible required quest, then the first visible
optional quest. `UI_ShowQuest` records the selected quest in
`client_s.quest_dialog` so `ForceQuestDialogUpdate` can refresh an already-open
journal without reopening a closed one or discarding the current selection.

Quest rows and objective rows are cloned lazily and then reused. This is
important because FDF frames live in a fixed `MAX_UI_CLASSES` pool; cloning a
new row tree on every quest click/refresh permanently consumes frame slots.
Unused cached rows are hidden before serialization.

The selected quest renders:

- its title in `QuestTitleValue`;
- its description in `QuestDisplay`;
- its objectives in `QuestItemListContainer`;
- authored selected/completed/failed row highlights when those named children
  exist in the retail FDF.

If an authored selected-highlight child is absent (notably small test fixtures),
the existing `> ` / yellow-title fallback remains in use.

## Quest Update Semantics

`ForceQuestDialogUpdate` is an update operation, not an open operation:

```text
closed journal -> no UI write
open journal + selected quest still visible -> refresh same quest
open journal + selected quest removed/hidden -> choose next visible quest
open journal + no visible quests -> close/clear the quest layer
```

`FlashQuestDialogButton` remains intentionally unimplemented. The current
server-authored `uiFrame_t` wire does not carry the stock button pulse/highlight
state, so using `ps.uiflags` or quest state as a substitute would encode the
wrong contract.

The current wire likewise does not serialize FDF `disabled` state. Therefore
the top Quests button is clickable even when there is no visible quest; the
`quests` command then has no dialog to show. Do not fake disabled behavior by
removing or changing quest state.

## Message Log Data Flow

`DisplayTextToPlayer`, `DisplayTimedTextToPlayer`, and
`DisplayTimedTextFromPlayer` use `UI_ShowText`:

```text
resolve TRIGSTR / level string
        -> format display text
        -> store transient client_s.message
        -> append same formatted text to client_s.message_log
        -> presentation_dirty flushes LAYER_MESSAGE
```

`client_s.message_log` is a 128-entry ring. When full, the oldest entry is
replaced. This implements bounded Warcraft-style history without coupling the
history lifetime to transient HUD messages.

`ClearTextMessages` only clears `client_s.message`:

```text
transient message -> cleared
message_log       -> preserved
```

`G_ShowCommandErrorText` deliberately calls `UI_ShowTransientText`; resource,
placement, cooldown, and similar command errors therefore remain transient and
do not pollute F12-style history.

## Log Dialog

The `log` command opens the stock `LogDialog` on `LAYER_LOGDIALOG`.
`hidelog` clears that layer. The authored `LogOkButton` is wired to `hidelog`.

The ring is assembled in chronological order into `LogArea`, separated by
blank lines. If new `DisplayText*` content arrives while the dialog is open,
`message_log.dirty` is flushed by `G_RunClients`, following the same
state-then-network-write discipline used by dialogue presentation.

`LAYER_LOGDIALOG` was appended to `UILAYOUTLAYER`; existing layer numeric IDs
must remain stable.

## Known Gaps

- No F9/F12 mapping is provided here. Another input-mapping path owns it.
- The fourth upper button is wired to the single-player `log` command. Runtime
  selection between single-player Log and multiplayer Chat is not yet modeled.
- The client `FT_TEXTAREA` path lays out/clips text but does not currently apply
  interactive scrollbar offsets. `LogAreaScrollBar` and the Quest scrollbars
  may render from FDF, but true long-history/list scrolling requires client UI
  input/scroll support rather than a server-side workaround.
- Message history is bounded by 128 logical entries. Retail's FDF expresses a
  128-line text-area limit; wrapped-line-equivalent eviction is not yet modeled.
- The Quest icon path is stored by `QuestSetIconPath` but is not yet bound to an
  authenticated child texture frame in the server-authored Quest row.
- Quest-button flashing and disabled-state visuals require additional frame
  presentation state in the network UI contract.
- Single-player menu-pause behavior for the Message Log is not implemented;
  `PauseGame` is itself still a stub.
- Save-game serialization is not yet available for quest/dialog/log state.

## Verification

Relevant tests live under `games/warcraft-3/game/tests/`:

- `wc3_game.hud_quest_visibility_requires_enabled_and_discovered`
- `wc3_game.hud_quest_rows_bind_authored_children`
- `wc3_api.display_text_tracks_lifetime_and_clear`
- `wc3_api.transient_command_style_text_does_not_enter_message_log`
- `wc3_api.message_log_is_bounded_and_evicts_oldest_entry`

When validating this work, run the normal WC3 test target and then verify a
campaign map manually:

```sh
make test-wc3-engine
```

Manual checks:

1. Click **Quests** with discovered/enabled required and optional quests; hidden
   or disabled quests must not appear.
2. Change objectives, call `ForceQuestDialogUpdate`, and confirm an open journal
   preserves its current visible selection.
3. Click another quest repeatedly and confirm rows do not duplicate.
4. Confirm the description and objective list follow the selected quest.
5. Display timed game text, clear/expire the transient message, click the fourth
   upper button, and confirm the historical text remains in Message Log.
6. Trigger a command error and confirm it is not added to Message Log.

## See Also

- [Triggered Dialogue](triggered-dialogue.md)
- [Warcraft III UI System](architecture/ui.md)
- [Server-Authored UI Payloads](../../architecture/ui-payloads.md)
- [FDF Reference](file-docs/fdf.md)
