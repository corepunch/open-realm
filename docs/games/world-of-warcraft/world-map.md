# World map

Press M or issue `worldmap` to open and close the current-area map. The game module reads `DBFilesClient\\WorldMapArea.dbc` from the installed game data and selects the smallest nondegenerate rectangle on the current physical map that contains the player's position. This is a bounds-based approximation where area rectangles overlap. Rows without valid map art names or finite dimensions are skipped. If no area can be resolved, the map window reports that the map is unavailable.

The **Continent** button selects the enclosing continent entry while keeping the player marker live. **Current zone** resumes the smallest-area view. **Zones** opens a paginated list of mapped zones on the same physical map. Select one to keep it open while moving. The player marker appears only when the player is inside its bounds. Zone names come from the installed `AreaTable.dbc`, with the map asset name as a fallback. Closing and reopening the map returns to the current zone.

The classic database has eight 32-bit fields per row. Fields 1 and 3 give the physical map ID and map asset directory, respectively. Fields 4–5 are the world Y bounds from left to right and fields 6–7 are the world X bounds from top to bottom. The selected row projects world position to the map marker. The 1002×668 detail image is reconstructed from twelve 256×256 textures in four columns and three rows. The rightmost column and bottom row crop unused pixels. The game loads the user's installed map artwork.

The map uses its own server-authored `LAYER_GAME_0`. The game refreshes the map image and marker once per second while visible. The zone list refreshes only on navigation. The selected view controls which bounds the refresh uses. Closing the map clears only this layer. Party markers and interactive map objects remain open.

The headless `test-wow-game` suite covers command opening and closing, the map tiles and marker, coordinate projection, the missing-data state, and transitions on the real frame scheduler. The game data fixture uses a small area within a larger continent to exercise overlap resolution.

## Landmarks

The **Landmarks** button opens a paginated list of named AreaPOI entries inside the displayed map rectangle. Select a name to return to the map and highlight its position. **Show landmarks** and **Hide landmarks** control the cyan markers independently of tracked quest markers. The selected landmark is shown in white with its name. The list and markers follow the selected zone or continent and are limited to the current physical map; entries outside the displayed rectangle are excluded. Missing or malformed landmark data leaves the map usable with an empty list.

The parser reads the installed classic 19-field `DBFilesClient\\AreaPOI.dbc`: fields 4 and 5 are world X and Y, field 7 is the physical map ID, and fields 10–17 are localized name offsets. The first valid nonempty installed locale is used. Row commands are revalidated against the currently displayed map and bounds before selection. Markers are clamped to the map border. No external landmark catalog or hardcoded locations are required.

The `world_map_landmarks_list_and_selection` game test exercises a localized fixture, map filtering, pagination, selection, highlighting and the visibility toggle through real commands.

## Exploration

The game samples the area ID of the terrain chunk under the player twice per second, including while the map is closed. A visit reveals every overlay in the displayed map area that lists this subzone in one of its four AreaTable fields. Discovery belongs to the character. Looking at a remote zone does not explore it.

The map first draws its twelve base tiles, then its discovered subzones, landmarks, quest pins, and player marker. The overlay's position and extent come from the Classic 17-field `WorldMapOverlay.dbc`. Large overlays use several BLPs in row-major order. A partial tile can have additional padding in the installed BLP. The game reads and caches that texture's actual dimensions when first needed so the visible pixels and map borders line up.
