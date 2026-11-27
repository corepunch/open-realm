#!/usr/bin/env python3
"""Check curated recipes against a locally mounted Classic Spell.dbc archive."""
import pathlib
import struct
import subprocess

root = pathlib.Path(__file__).resolve().parents[3]
archive = root / "data/world-of-warcraft/dbc.MPQ"
tool = root / "build/bin/mpqtool"
data = subprocess.check_output([str(tool), "-mpq", str(archive), "cat", "DBFilesClient/Spell.dbc"])
magic, count, fields, size, _ = struct.unpack_from("<4s4I", data)
assert magic == b"WDBC" and fields >= 98 and size == fields * 4
rows = {row[0]: row for row in (struct.unpack_from(f"<{fields}I", data, 20 + i * size) for i in range(count)) if row[0] in (3275, 3276, 746, 1159, 11196)}
for spell, count, result in ((3275, 1, 1251), (3276, 2, 2581)):
    row = rows[spell]
    assert row[38] == 2589 and row[46] == count
    assert row[56] == 24 and row[95] == result
    assert all(row[i] == 0 for i in range(39, 46))
for spell, base in ((746, 10), (1159, 18)):
    row = rows[spell]
    assert row[26] == 32 and row[56] == 6 and row[71] == base
    assert row[83] == 8 and row[86] == 1000
assert rows[11196][26] == 3
duration = subprocess.check_output([str(tool), "-mpq", str(archive), "cat", "DBFilesClient/SpellDuration.dbc"])
magic, count, fields, size, _ = struct.unpack_from("<4s4I", duration)
assert magic == b"WDBC" and size == fields * 4
durations = {row[0]: row[1] for row in (struct.unpack_from(f"<{fields}I", duration, 20 + i * size) for i in range(count))}
assert durations[32] == 6000 and durations[3] == 60000
print("Verified linen bandage recipes, healing and duration against installed DBC")
