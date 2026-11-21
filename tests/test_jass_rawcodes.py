"""Cross-check of the rawcode native table (api_rawcode.h) against script signatures.

Scripts see rawcodes as big-endian integers; the VM converts the arguments and
results of the natives listed in the table. A native that takes an id-like
integer but is missing from the table would silently receive a byte-swapped id.

By default the check reads the fixture scripts and bundled declarations for registered natives. To check the
real game scripts, point JASS_SCRIPTS_DIRS at directories holding common.j,
common.ai and Blizzard.j (path separator ':'), for example the files extracted
from War3.mpq and War3x.mpq of 1.29.
"""

from __future__ import annotations

import os
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WC3 = ROOT / "games/warcraft-3"
TABLE = WC3 / "game/api/api_rawcode.h"
REGISTRY = WC3 / "game/api/api_module.c"
DECLARATIONS = WC3 / "game/common.txt"
ORDERS = WC3 / "game/m_unit.c"
FIXTURES = WC3 / "tests/resources-src/Scripts"

NATIVE = re.compile(r"native\s+(\w+)\s+takes\s+(.*?)\s+returns\s+(\w+)")
# Integer parameters with these names carry object rawcodes.
RAWCODE_PARAMS = {
    "abilcode", "abilid", "abilityid", "buffid", "doodadid", "effectid", "itemid", "objectid",
    "portraitunitid", "skinid", "techid", "unitid", "unittype", "terraintype", "uid",
}
# Integer parameters named "id" in common.ai that are unit, upgrade or building rawcodes.
AI_ID_PARAMS = {
    "GetTownUnitCount", "GetUpgradeLevel", "GetUpgradeGoldCost", "GetUpgradeWoodCost", "GetUpgradeLumberCost", "SetProduce",
    "SetExpansion", "SetUpgrade", "ConvertUnits", "AddAssault", "AddDefenders", "AddGuardPost", "MergeUnits",
}
# Integer parameters that look id-like but are NOT rawcodes (native, 1-based index). Keep each with its reason.
NOT_RAWCODE = {
    ("CommandAI", 2): "AI command code, a script-defined number",
    ("CommandAI", 3): "AI command data, opaque to the engine",
    ("GetPlayerId", 0): "player index",
}
# Table bits that point at an integer whose name is not in the sets above (native, 1-based index).
# Keep each with its reason; empty means every bit matches a rawcode-like or order-like name.
EXTRA_BITS = {
    ("DebugUnitID", 2): "common.ai debug print of a unit rawcode, parameter is named val",
}
# Natives that return rawcodes.
RAWCODE_RESULTS = {
    "GetUnitTypeId", "GetItemTypeId", "GetDestructableTypeId", "GetSpellAbilityId", "GetLearnedSkill",
    "GetResearched", "GetTrainedUnitType", "GetHeroId", "UnitId", "AbilityId", "ChooseRandomCreep",
    "ChooseRandomItem", "ChooseRandomItemEx", "ChooseRandomNPBuilding", "GetTerrainType", "GetItemDropID",
}
# Order-id natives: the integer may be a numeric order or a unit rawcode (build, train).
ORDER_PARAMS = {"order", "orderid"}
ORDER_RESULTS = {"GetIssuedOrderId", "GetUnitCurrentOrder", "OrderId"}
# Integer parameters that are rawcode-free although named like orders (kept explicit).
NOT_ORDER = {"OrderId2String"}


def parse_table():
    table = {}
    text = TABLE.read_text()
    for m in re.finditer(r'\{\s*"(\w+)",\s*([^,]+?),\s*([^}]+?)\s*\}', text):
        name, mask, order = m.groups()
        if name in table:
            raise ValueError(f"duplicate rawcode native: {name}")
        def bits(expr):
            value = 0
            for arg in re.findall(r"JASS_RAWCODE_ARG\((\d+)\)", expr):
                index = int(arg)
                if not 1 <= index <= 31:
                    raise ValueError(f"{name}: rawcode argument {index} outside 1..31")
                value |= 1 << (index - 1)
            if "JASS_RAWCODE_RESULT" in expr:
                value |= 1 << 31
            return value
        table[name] = (bits(mask), bits(order))
    return table


def script_files():
    dirs = [FIXTURES] + [Path(p) for p in os.environ.get("JASS_SCRIPTS_DIRS", "").split(":") if p]
    for directory in dirs:
        for name in ("common.j", "common.ai"):
            path = directory / name
            if path.exists():
                yield path


def signatures():
    registered = set(re.findall(r'\{\s*"(\w+)"\s*,', REGISTRY.read_text()))
    for path in [DECLARATIONS, *script_files()]:
        for line in path.read_text(errors="replace").splitlines():
            m = NATIVE.search(line)
            if not m:
                continue
            name, params, result = m.groups()
            if path == DECLARATIONS and name not in registered:
                continue
            args = [p.split() for p in params.split(",")] if params != "nothing" else []
            yield path, name, [(i + 1, t, n) for i, (t, n) in enumerate(args)], result


class RawcodeTableTest(unittest.TestCase):
    def test_table_is_sorted_and_names_are_unique(self):
        names = list(parse_table())
        self.assertEqual(names, sorted(names))

    def test_every_id_like_parameter_is_in_the_table(self):
        table = parse_table()
        missing = []
        for path, name, args, result in signatures():
            mask, order = table.get(name, (0, 0))
            for index, kind, param in args:
                if kind != "integer" or (name, index) in NOT_RAWCODE:
                    continue
                lowered = param.lower()
                if lowered in RAWCODE_PARAMS or (lowered in ("id", "a", "b", "make") and name in AI_ID_PARAMS):
                    if not mask & (1 << (index - 1)):
                        missing.append(f"{name}:{index} ({param}) in {path.name}")
                elif lowered in ORDER_PARAMS and name not in NOT_ORDER:
                    if not order & (1 << (index - 1)):
                        missing.append(f"{name}:{index} ({param}, order id) in {path.name}")
            if name in RAWCODE_RESULTS and not mask & (1 << 31):
                missing.append(f"{name} result in {path.name}")
            if name in ORDER_RESULTS and not order & (1 << 31):
                missing.append(f"{name} order result in {path.name}")
        self.assertEqual(missing, [])

    def test_every_table_bit_points_at_a_rawcode_or_order_slot(self):
        # The reverse check: a bit on the wrong parameter (or on a non-integer one)
        # would swap an innocent value. Natives absent from the scanned scripts
        # (OpenRealm-only ones) are skipped.
        table = parse_table()
        wrong = []
        for path, name, args, result in signatures():
            if name not in table:
                continue
            mask, order = table[name]
            for kind_name, bits in (("rawcode", mask), ("order", order)):
                for index in range(1, 32):
                    if not bits & (1 << (index - 1)):
                        continue
                    if index > len(args):
                        wrong.append(f"{path}: {name}:{index} ({kind_name}) has no such parameter")
                        continue
                    _, kind, param = args[index - 1]
                    lowered = param.lower()
                    if kind != "integer":
                        wrong.append(f"{path}: {name}:{index} ({param}) is {kind}, not integer")
                    elif kind_name == "order":
                        if lowered not in ORDER_PARAMS:
                            wrong.append(f"{path}: {name}:{index} ({param}) marked as order id")
                    elif not ((name, index) in EXTRA_BITS or lowered in RAWCODE_PARAMS
                              or (lowered in ("id", "a", "b", "make") and name in AI_ID_PARAMS)):
                        wrong.append(f"{path}: {name}:{index} ({param}) marked as rawcode")
                if bits & (1 << 31):
                    if result != "integer":
                        wrong.append(f"{path}: {name} result ({result}) is not integer")
                    elif kind_name == "order" and name not in ORDER_RESULTS:
                        wrong.append(f"{path}: {name} result marked as order id")
                    elif kind_name == "rawcode" and name not in RAWCODE_RESULTS:
                        wrong.append(f"{path}: {name} result marked as rawcode")
        self.assertEqual(wrong, [])

    def test_every_script_host_passes_the_rawcode_table(self):
        # A host that has the natives but no rawcode table runs literals big-endian
        # and natives little-endian (the +set jass_test mode once did).
        bad = []
        for path in sorted(WC3.rglob("*.c")):
            text = path.read_text(errors="replace")
            for m in re.finditer(r"jass_sethost\(", text):
                block = text[m.start():m.start() + 700]
                end = block.find("));")
                block = block[:end if end >= 0 else len(block)]
                if ".natives" in block and "jass_funcs" in block and ".rawcodes" not in block:
                    bad.append(f"{path.relative_to(ROOT)}:{text[:m.start()].count(chr(10)) + 1}")
        self.assertEqual(bad, [])

    def test_numeric_order_ids_stay_below_the_rawcode_range(self):
        # Order-id slots convert only values >= 0x01000000; every order id of the
        # engine's table (the largest is 852597) must stay below that.
        ids = [int(v) for v in re.findall(r'\{\s*"\w+",\s*(\d+),', ORDERS.read_text())]
        self.assertGreater(len(ids), 50)
        self.assertLess(max(ids), 0x01000000)
        # The smallest four-character rawcode with a printable first letter.
        self.assertGreater(int.from_bytes(b"0000", "big"), max(ids))


if __name__ == "__main__":
    sys.dont_write_bytecode = True
    unittest.main()
