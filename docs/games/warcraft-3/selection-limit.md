# Warcraft III Selection Limit

`wc3_selection_limit` defaults to `24` (Reforged-style). Set it to exactly `12` for classic mode; other values use the default 24. Warcraft gameplay owns the authoritative selection cap, not the generic client. WC3 selection buffers support 24, while the generic client supports 64.

The regular `G_UpdateClientSelections` pass removes excess selected units when the limit is lowered, emits deselection events, and synchronizes the trimmed selection/portrait/commands to the client. Direct `G_SelectEntity` insertion also enforces the cap. Selection changes already in progress are validated by the server.

`cl_selection_limit` is a **separate generic client input setting** and defaults to 24 in the Warcraft configuration. For 12-unit **local box selection** (rather than the server rejecting excess members), also set `cl_selection_limit 12`. Restoring 24 similarly requires `cl_selection_limit 24` if you changed it. Do not bind generic client code to Warcraft-specific CVars; the server remains authoritative even if these settings differ. An existing user config can override shipped defaults.

Marquee selection gathers and sends up to 63 candidates to the server. Snapshot `EF_UNIT` marks gameplay units, while recipient-relative `EF_ALLIED` distinguishes allies from passive neutrals. `R_EntitiesInRect` returns player-owned units first, then allied units, then other units, followed by scene props and structures. Entity flags are 32-bit on protocol 18 so these markers survive the snapshot. This ordering keeps early map entities and unrelated units from crowding the player's force out of the bounded candidate list, while still allowing hostile-unit and structures-only selection. The server filters every candidate for visibility, eligibility, and control before applying the configured limit; the client prediction remains capped by `cl_selection_limit` until authoritative reconciliation. The 63-candidate cap leaves one of the command parser's 64 tokens for the `select` command name. If more than 63 player-owned or allied units overlap the marquee, the candidate cap still applies.

Warcraft III Reforged supports selecting up to 24 units starting with patch 3.0.0. The classic 12-unit option is retained for compatibility.

At 1–12 selected units, the multiselect panel retains the original 6×2 layout. At 13–24, it uses an OpenRealm 8×3 compact grid. This is **not** a claim of retail-exact Reforged geometry. Portrait hit tests use the same frame rect and offsets as drawing. Visual inspection at 640×480 is required.

Manual checks: box-select 12/13/18/24/25 units; validate cap, 8×3 icons, click targets, bars and subgroup highlight; use Tab, Shift-click, double-click collapse, group assign/recall and movement/attack orders. Change `wc3_selection_limit` from 24 to 12 while 24 units are selected and verify trims/events/HUD/commands; repeat with `cl_selection_limit 12`. Verify map reload, 640×480 and pre-existing user configs.
