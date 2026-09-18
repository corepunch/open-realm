"""Tests for the Warcraft III hero walk/save/load campaign audit."""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "wc3_hero_saveload_audit", ROOT / "tools/wc3_hero_saveload_audit.py")
AUDIT = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(AUDIT)


class ParseTest(unittest.TestCase):
    def test_status_and_snapshots_are_parsed(self):
        output = """
loading map Maps/Campaign/Human02.w3m
HERO_SAVELOAD snapshot=before unit=12 class=Hpal origin=10.00,20.00,0.00 abilities=AHhb:1 added=- inventory=spro:3 move=walk
HERO_SAVELOAD snapshot=after unit=12 class=Hpal origin=10.00,20.00,0.00 abilities=AHhb:1 added=- inventory=spro:3 move=walk
HERO_SAVELOAD status=pass unit=12 class=Hpal origin=10.00,20.00,0.00 abilities=AHhb:1 added=- inventory=spro:3 move=walk
"""
        parsed = AUDIT.parse_hero_saveload(output)
        self.assertEqual(parsed["status"], "pass")
        self.assertIn("class=Hpal", parsed["before"])
        self.assertIn("inventory=spro:3", parsed["after"])

    def test_incomplete_when_status_line_missing(self):
        parsed = AUDIT.classify("loading map\n", 0, False)
        self.assertEqual(parsed["status"], "incomplete")
        self.assertEqual(parsed["families"], ["incomplete"])

    def test_signal_crash_is_reported(self):
        parsed = AUDIT.classify("", -11, False)
        self.assertEqual(parsed["status"], "crashed")
        self.assertEqual(parsed["families"], ["SIGSEGV"])

    def test_timeout_is_reported(self):
        parsed = AUDIT.classify("HERO_SAVELOAD snapshot=before unit=1", None, True)
        self.assertEqual(parsed["status"], "timeout")


class ReportTest(unittest.TestCase):
    def test_report_separates_pass_from_no_hero(self):
        report = {
            "commit": "abc123",
            "frames": 400,
            "simulated_seconds": 40,
            "jobs": 4,
            "wall_seconds": 1.5,
            "maps": [
                {
                    "edition": "RoC", "name": "Chapter One", "filename": "Human01.w3m",
                    "status": "pass", "families": ["pass"],
                    "before": "unit=12 class=Hpal", "after": "unit=12 class=Hpal",
                },
                {
                    "edition": "RoC", "name": "Interlude", "filename": "Human01Interlude.w3m",
                    "status": "no_hero", "families": ["no_hero"], "before": "", "after": "",
                },
            ],
        }
        markdown = AUDIT.render_markdown(report)
        self.assertIn("**1 passed, 1 had no live Hero, 0 failed or crashed**", markdown)
        self.assertIn("`Human01.w3m`", markdown)
        self.assertNotIn("mission complete", markdown.lower())


if __name__ == "__main__":
    unittest.main()
