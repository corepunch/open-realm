# Flight paths

The game reads `TaxiNodes.dbc`, `TaxiPath.dbc` and `TaxiPathNode.dbc` from installed client data. The decoded graph owns its records; path points are sorted by path and sequence. Malformed file envelopes are rejected, and invalid rows are skipped. The game makes a node available when the player interacts within ten yards of an imported flight master, resolving the nearest same-map node within 100 yards of that NPC. The HUD button or T also discovers a node within ten yards of the player; discovered destinations reachable through discovered nodes appear with their total copper fare.

Selecting a destination validates proximity, discovery, available balance and a directed lowest-fare route again on the server. The fare is charged once before flight. The server moves the player along the authored points, suppressing player commands until arrival. Flight and the window are cleared on map load. Cross-map legs are excluded until map transition during flight is supported.

Discovered nodes are saved for the selected character. There is no faction or mount filtering, mount presentation, flight point event handling or cross-map continuation. The current creature spawn extract has no flight-master spawn rows, so the proximity interaction is the reliable path with the bundled data. The service spawn pipeline accepts such rows when available; quest-spawned flight masters can also be used. Hostile damage and combat targeting are suppressed during flight. Paths that include a map change are unavailable. Point delay values are treated as seconds.

A smoke check against the installed `dbc.MPQ` decoded 69 nodes, 184 paths and 7,411 retained points, and found a route from node 26 to node 27. That client’s TaxiNodes table has 14 fields and no mount/faction columns, so faction eligibility needs a separate authoritative source.

Run the graph and flight integration tests with `make test-wow-game`.
