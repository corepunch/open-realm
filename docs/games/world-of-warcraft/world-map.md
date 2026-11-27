# World map

Press M or issue `worldmap` to open and close the current-area map. The game module reads `DBFilesClient\\WorldMapArea.dbc` from the installed game data and selects the smallest nondegenerate rectangle on the current physical map that contains the player's position. This is a bounds-based approximation where area rectangles overlap. Rows without valid map art names or finite dimensions are skipped. If no area can be resolved, the map window reports that the map is unavailable.

The classic database has eight 32-bit fields per row. Fields 1 and 3 give the physical map ID and map asset directory, respectively. Fields 4–5 are the world Y bounds from left to right and fields 6–7 are the world X bounds from top to bottom. The selected row projects world position to the map marker. The 1002×668 detail image is reconstructed from twelve 256×256 textures in four columns and three rows. The rightmost column and bottom row crop unused pixels. The game loads the user's installed map artwork.

The map uses its own server-authored `LAYER_GAME_0`, refreshed once per second while open. Resolving the area during refresh allows the map to update when a player crosses area bounds and when entering a smaller area contained by a continent rectangle. Closing the map clears only this layer. The implementation does not yet provide map browsing, sub-zone boundaries, overlays, party markers, or map object interaction.

The headless `test-wow-game` suite covers command opening and closing, the map tiles and marker, coordinate projection, the missing-data state, and transitions on the real frame scheduler. The game data fixture uses a small area within a larger continent to exercise overlap resolution.
