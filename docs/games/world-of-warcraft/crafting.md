# First Aid crafting

The First Aid button opens a portable server-authored crafting panel. Kobolds can drop Linen Cloth (item 2589); one cloth makes a Linen Bandage (item 1251), and two make a Heavy Linen Bandage (item 2581). The server requires the panel to be open and a living player, validates the requested recipe, and consumes reagents across stacks. It prepares the transaction on a copy of inventory using the shared storage APIs, then commits only if the output fits. A full bag can still accept the output when consumption frees a slot.

The recipe rows are bounded to two entries from the installed `DBFilesClient/Spell.dbc`: spell 3275 creates 1251 from one 2589, and spell 3276 creates 2581 from two 2589. `python3 games/world-of-warcraft/tests/validate_crafting_dbc.py` checks the records in `data/world-of-warcraft/dbc.MPQ` using `build/bin/mpqtool`.

First Aid training, skill requirements, and crafting cast time are not yet implemented. The panel is portable because these two spells do not require a crafting station. Its recipe availability is currently a prototype and does not represent profession progression.

Using a bandage on yourself consumes it and starts six one-second healing ticks: 11 health for Linen (spell 746), 19 for Heavy Linen (1159). Movement, damage, or successfully starting another spell interrupts the remaining ticks. Full health, movement, casting, and Recently Bandaged prevent starting; the 60-second Recently Bandaged timer (11196) starts on application and survives interruption. The Spell.dbc effect base points and amplitude and SpellDuration.dbc duration rows 32 and 3 supply these values. This prototype caps player health at 100, so these unscaled Classic values are powerful relative to the current health model.
