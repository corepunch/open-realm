# Warcraft III Campaign Progress

## Retail persistence split

Warcraft III keeps campaign-menu progression separate from the JASS `gamecache`
used to carry Heroes, variables, and items between maps.

Classic/profile-era installs store both files in the active Warcraft III profile
save directory. Later classic installs commonly use a path such as:

```text
%USERPROFILE%\Saved Games\Warcraft III\Profile1\Campaigns.w3p
%USERPROFILE%\Saved Games\Warcraft III\Profile1\Campaigns.w3v
```

Reforged uses the Battle.net campaign directory under Documents and separates
Classic/Reforged progression, for example:

```text
Documents\Warcraft III\BattleNet\<account>\Campaigns\Classic\Classic.w3p
Documents\Warcraft III\BattleNet\<account>\Campaigns\Reforged\Reforged.w3p
```

The `.w3p` file is the frontend campaign-progress record: unlocked campaigns and
mission progression. The `.w3v` file is the game-cache container used by map
scripts for state carried between campaign maps. See
[campaign-game-cache.md](campaign-game-cache.md) for OpenRealm's `.w3v`-equivalent
runtime and Hero persistence.

OpenRealm does not claim binary compatibility with retail `.w3p` or `.w3v`
files. Both are represented by private, versioned sidecars.

## OpenRealm ownership and file

Campaign/menu availability is persisted in:

```text
campaign-progress.orcp
```

The filename is resolved through the engine-owned `FS_UserPath()` policy. The
game receives that boundary as `game_import.UserPath`; the menu receives the
same resolver as `menuImport_t.UserPath`. This follows the same ownership rule
as campaign game-cache persistence: DLLs request a writable per-game path, but
do not know the platform-specific user-data directory themselves.

The common serializer lives in:

- `games/warcraft-3/common/campaign_progress.c`
- `games/warcraft-3/common/campaign_progress.h`

Game-side native integration lives in:

- `games/warcraft-3/game/g_campaign_progress.c`
- `games/warcraft-3/game/api/api_misc.h`

The campaign selector consumes the same file in:

- `games/warcraft-3/menu/screens/single_player.c`

The private file uses the FOURCC `ORCP` magic and version `1`. Its integer header is
explicit little-endian and the payload stores bounded known/value bits for:

- tutorial-cleared state;
- campaign availability;
- mission availability.

The separate known bits matter because authored `false` writes must override
`DefaultOpen`; an explicit lock is not the same state as a fresh profile with no
saved value.

Writes go to a temporary file and are renamed into place after a complete write.
The previous file is preserved as a temporary backup during replacement so a
failed install does not silently leave a partially written progress record.

## Campaign indices

The native indices are the values Blizzard.j ultimately passes to
`SetCampaignAvailable` / `SetMissionAvailable`, not always the public BJ campaign
constants.

Reign of Chaos uses:

| Native campaign | Campaign key |
| ---: | --- |
| 0 | `Tutorial` |
| 1 | `Human` |
| 2 | `Undead` |
| 3 | `Orc` |
| 4 | `NightElf` |

The Frozen Throne wrappers convert the expansion BJ constants to offsets before
calling the natives:

| Native campaign | Campaign key |
| ---: | --- |
| 0 | `NightElf` |
| 1 | `Human` |
| 2 | `Undead` |
| 3 | `Orc` |

RoC and TFT progress are therefore stored as separate editions inside the same
OpenRealm sidecar.

## JASS update boundary

These natives immediately update and commit the campaign-progress sidecar:

```text
SetTutorialCleared(cleared)
SetCampaignAvailable(campaignNumber, available)
SetMissionAvailable(campaignNumber, missionNumber, available)
```

This is intentionally different from JASS `gamecache`, where `Store*()` remains
handle-local until an explicit `SaveGameCache()` call. Campaign availability is
frontend/profile state authored directly by these availability natives.

The Prologue02 ending is the concrete compatibility case that established this
contract. Its `Trig_NextLevelPrep_Actions` calls Blizzard's wrappers to make the
Human campaign and Human mission 0 available. Those wrappers reach:

```text
SetCampaignAvailable(1, true)
SetMissionAvailable(1, 0, true)
```

before the long ending cinematic finishes. OpenRealm writes both unlocks at that
point. `ForceCampaignSelectScreen()` only selects the destination for the later
`EndGame`, so the cinematic can continue while the newly unlocked state is
already durable.

## Campaign selector visibility

The game-owned archived CVar is initialized in `games/warcraft-3/share/config.cfg`:

```text
wc3_campaign_visibility all
wc3_campaign_visibility unlocked
```

Default: `all`.

`all` keeps the developer-friendly behavior of showing every parsed campaign and
map-backed mission regardless of profile progress.

`unlocked` uses authored/persisted availability:

- an explicit `SetCampaignAvailable` value wins when one has been written;
- otherwise a campaign with `DefaultOpen=1` is shown;
- an explicit `SetMissionAvailable` value wins when one has been written;
- otherwise mission 0 is shown when its campaign is available.

`DefaultOpen` is parsed from the active `CampaignStrings` data rather than being
hard-coded by race. Campaign and mission rows are rebuilt/reloaded when entering
the campaign selector, so progress written by the preceding map is visible after
the session returns to the frontend.

### Selector rows

Patched clients build both selector columns at runtime from `CampaignStrings` (Warsmash's `CampaignMenuUI` does the same); the static `TutorialFrame` … `NightElfFrame` and `Mission0Frame` … `Mission13Frame` rows authored by the 1.00 `CampaignMenu.fdf` are always hidden. Each row clones `CampaignArrowButtonTemplate` (maps, campaigns) or `CampaignCameraButtonTemplate` (movies), `StandardSmallTextTemplate` for the gold header and `StandardTitleTextTemplate` (grey 0.764) for the name, laid out like the 1.00 rows.

Rows occupy fixed 0.0315625 slots. The campaign box has ten slots (the height of TFT `CampaignListBox.fdf`'s scroll bar, 0.315625), the mission box sixteen; both are centred 0.2852 below `CampaignMenu`'s top-right with their left edge at -0.287 (the campaign box's top-left is `(-0.287, -0.1274)`). Retail 1.2x shows the whole Human column (14 rows, 0.032 apart) without scrolling, so a ten-slot mission box was wrong: the row pitch and text already match retail, only the slot count differed. Every campaign is followed by a blank slot, and an Introduction cinematic by one blank slot. A column that fits is centred in the box; a longer one starts at the top and scrolls with the mouse wheel (the screen's `scroll` hook). These constants were fitted to retail 1.2x captures: two campaigns centre at 0.236/0.301 below the top edge, and the Prologue mission column (Intro, blank, Opening, two chapters) centres at 0.285 with 0.032 spacing, both within about a pixel. The earlier static-row layout sat ~0.03 higher because the 1.00 FDF rows are not what patched clients draw. The `WarCraftIIILogo` sprite's FDF offset leaves it off-screen, so the menu loads `MainMenuLogo` and anchors it at `TOPRIGHT (-0.13, -0.08)`, mirroring the main menu (and Warsmash).

Choosing a campaign swaps the backdrop to that campaign's scene and plays its `Birth` camera move; Back from the mission column restores the selector's default-campaign scene with the same move.

The 1.00 `War3.mpq` `CampaignStrings.txt` also predates the later schema: it has no `[Index]`, `Background`, `Cursor`, or `DefaultOpen`, splits missions into `TitleN`/`MissionN`/`FileN`, and names cinematics only by title (`InCinematic`/`OpCinematic`/`EdCinematic`, headers in `[Label]`). The parser maps those to `<Key>In/Op/Ed` movies and the `<Key>Backdrop` skin, printing a console warning for each campaign that falls back. Without `DefaultOpen`, `unlocked` visibility shows nothing until progress exists; the default `all` shows every campaign.

The older `wc3_campaign_mission_visibility=played` and
`wc3_campaign_played_<campaign>_<mission>` frontend bridge is removed. Merely
launching a map is not campaign progression; authored JASS availability natives
are authoritative.

#### Comparing full retail mission columns

The retail RoC references show Human's 14 rows, Undead's 11, Orc's 11, and Night Elf's 9 without scrolling,
including opening/ending movie rows. The old ten-slot mission box truncated Human and the other longer columns;
[PR #581](https://github.com/corepunch/open-realm/pull/581) provides sixteen mission slots while retaining ten campaign
slots, the row pitch, and the shared column centre. This is a box-capacity issue, not an icon-position or text-size issue.

For framebuffer comparison, initialize Single Player before choosing a campaign. `menu_single_player_campaign` opens
the campaign selector; the named shortcut opens its mission list. Skip the authored entrance animation to capture the
settled backdrop, and use a temporary profile so diagnostics do not change normal user settings:

```sh
make openwarcraft3 FFMPEG=1
XDG_DATA_HOME=/tmp/wc3-campaign-check build/bin/openwarcraft3 -data 'data/Warcraft III' -vid_hidden 1 \
  +vid_native 0 +vid_mode 3 +ui_skip_transitions 1 +scr_showfps 0 \
  +menu_game +menu_single_player_campaign_human +screenshot 120 +com_frame_limit 135
```

Repeat with `undead`, `orc`, and `night_elf` in the shortcut suffix; add `-tft` to check the expansion overrides.
Read the newly written `screenshots/shotNNNN.jpg`. A sandbox without macOS display access can exit successfully with
`Drawable size: 0x0` and no screenshot; the command must have graphics access for framebuffer verification.
`FFMPEG=1` requires the five pkg-config packages listed in `games/warcraft-3/game.mk`. A build without it deliberately
omits movie rows, so its shorter column cannot establish parity with references containing those rows. For exact row
counts, also inspect the active archive's `UI/CampaignStrings.txt`; installed 1.00 RoC data uses `TitleN`/`MissionN`
and the legacy `OpCinematic`/`EdCinematic` schema.

`make test-menu` covers both RoC/TFT selector geometry, named console commands, re-entry, and wheel scrolling.
Run it in both movie-enabled and default builds when reviewing changes to the complete mission column. These tests
verify layout and input contracts; framebuffer inspection covers the authored fonts, icon artwork, and backdrop.

The Orc campaign sky's authored static `GEOA` RGB is approximately `(0.968628, 0.462745, 0.0588235)` on geoset 10.
The former static-color evaluator reversed red and blue, making this orange sky blue and Night Elf's cool light beams
warm. Static base colors now pass through as RGB; animated `KGAC` keys retain their BGR-to-RGB conversion. Warsmash's
constructor swap is cancelled by its shader's `.bgra` read, so reproducing only the constructor was incorrect. See
[MDX color conventions](time-of-day.md#mdx-light-track-compatibility).

`renderer_model.static_geoset_colors_preserve_rgb_from_mdx` reads the stock-shaped sky record and a non-stock blue
record through `ReadGeosetAnim` before evaluating the tint. Existing animated-color tests protect `KGAC` and DNC
behavior. The stock and non-stock static cases both failed before the fix. The framebuffer comparison must use the
final campaign scene to verify the archive's textured sky/beam appearance; unit tests cover the color contract.
Bounded movie-enabled captures confirmed the orange RoC Orc sky, cool-blue RoC Night Elf beams/arrow, and the TFT
Night Elf override after this correction. Full mission columns remain visible in those scenes.

## Verification

Regression coverage verifies:

- the JASS availability natives create/update the progress file;
- the persisted RoC Human campaign/mission bits survive a file reload;
- RoC/TFT campaign string keys map to Blizzard's native campaign indices;
- the menu defaults to showing all campaign content;
- `unlocked` mode shows `DefaultOpen` content plus campaigns/missions present in
  the progress sidecar;
- selecting a mission no longer creates a synthetic `played` CVar.

## Retail evidence used for this implementation

The retail file/path conclusions above are version-specific rather than a claim
that every Warcraft III release used one directory:

- Blizzard's Warcraft III: Reforged forum documents Classic progress under
  `Documents\Warcraft III\BattleNet\<account>\Campaigns\Classic` and identifies
  `Classic.w3p` as the file containing unlocked campaigns and completed missions:
  <https://us.forums.blizzard.com/en/warcraft3/t/psa-backing-up-campaign-progress/22160>
- A Reforged support thread identifies the corresponding Reforged directory and
  the `Reforged.w3p` / `Campaigns.w3v` files needed when moving campaign state:
  <https://us.forums.blizzard.com/en/warcraft3/t/solvedwhere-are-the-campaigns-saved-games-stored/17387>
- The documented Warcraft III game-cache format identifies `Campaigns.w3v` as
  per-profile variables/units carried between campaign maps, not the selector's
  `.w3p` availability record:
  <https://alanfox2000software.github.io/war3-diy/doc/w3x/index.html>
- Later pre-Reforged classic installs have been observed with both
  `Campaigns.w3p` and `Campaigns.w3v` under
  `%USERPROFILE%\Saved Games\Warcraft III\ProfileN`:
  <https://forum.3ice.hu/viewtopic.php?p=6018>
