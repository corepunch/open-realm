# Runtime Map Unit Snapshot Export

The optional `wc3_runtime_export` cvar writes a tab-separated snapshot of live
unit entities after the map's JASS `main()` function and its initial queued-event
drain run. The game process writes the file. JASS code receives no file or
operating-system access.

```sh
build/bin/openwarcraft3 \
  -data /path/to/warcraft-iii-data \
  -tft +dedicated 1 \
  +set wc3_runtime_export /path/to/map-unitstats.tsv \
  +map 'Maps/Test/map.w3x' \
  +com_frame_limit 100
```

The exporter truncates the configured output path when the map's scripts start.
Use a path writable by the local game process. When the cvar is empty, the
exporter does no file I/O.

The TSV contains one row per live unit instance, with rawcode, entity number,
owner, current and maximum HP, current armor, attack 1 base/dice/sides/cooldown,
and gold/lumber bounty base/dice/sides. The snapshot preserves differences
between instances of the same rawcode.

This is OpenRealm state, not a capture from Blizzard's Warcraft III executable.
It contains units created during map initialization; later gameplay spawns are
not included. Renderer-only skin, fog, water, pitch/roll, and tint settings may
be ignored by the dedicated capture host. Event processing after the snapshot
continues normally and may report separate runtime limits.

## Verification

Headless runs with the installed 1.29.2.9231 data reached server initialization
and exported 321 live units from the classic WarChasers scenario and 155 from
the TFT Monolith scenario. These exercise the classic object-data readers.
The Reforged placement checks use generated v13 fixtures. The original Werewolf
Transylvania map and its editor build were unavailable for this follow-up.
