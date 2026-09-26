"""Run with python3 tests/test_rename_ability_callbacks.py; no game data required."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tools"))
from rename_ability_callbacks import rename_callbacks


SOURCE = '''// holy_validate must stay in this comment.
static char const *label = "holy_execute";
static bool holy_validate(edict_t *ent, spellTarget_t st) { return true; }
static void holy_execute(edict_t *ent, spellTarget_t st, ability_t const *ability) {}
ability_t CAbilityHolyBolt = {
    .validate = holy_validate,
    .execute = holy_execute,
};
'''


class RenameCallbacksTest(unittest.TestCase):
    def test_callback_tokens_and_repeat(self):
        result = rename_callbacks(SOURCE, "CAbilityHolyBolt")
        self.assertIn("static bool CAbilityHolyBolt_Validate(", result)
        self.assertIn(".execute = CAbilityHolyBolt_Execute", result)
        self.assertIn('// holy_validate must stay in this comment.', result)
        self.assertIn('"holy_execute"', result)
        self.assertEqual(rename_callbacks(result, "CAbilityHolyBolt"), result)

    def test_flat_definition_and_shared_references(self):
        source = SOURCE + "void test(void) { holy_execute(0, 0, 0); }"
        result = rename_callbacks(source, "CAbilityHolyBolt")
        self.assertIn("CAbilityHolyBolt_Execute(0, 0, 0)", result)

    def test_null_optional_validator(self):
        result = rename_callbacks(SOURCE.replace(".validate = holy_validate", ".validate = NULL"), "CAbilityHolyBolt")
        self.assertIn(".validate = NULL", result)
        self.assertIn("static bool holy_validate(", result)

    def test_unsupported_or_unsafe_rename(self):
        for source, reason in [
            (SOURCE + "int CAbilityHolyBolt_Execute;", "already exists"),
            (SOURCE.replace("static void holy_execute", "void holy_execute"), "static function"),
            ("SPELL(AbilityHolyBolt, holy_execute);", "macro-generated"),
        ]:
            with self.subTest(reason=reason), self.assertRaisesRegex(ValueError, reason):
                rename_callbacks(source, "CAbilityHolyBolt")

    def test_cli_preview_write_and_failure_preserves_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "spell.c"
            path.write_bytes(SOURCE.replace("\n", "\r\n").encode())
            original = path.read_bytes()
            cmd = [sys.executable, str(ROOT / "tools/rename_ability_callbacks.py"), str(path), "--ability", "CAbilityHolyBolt"]
            preview = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(preview.returncode, 0, preview.stderr)
            self.assertIn("+static bool CAbilityHolyBolt_Validate", preview.stdout)
            self.assertEqual(path.read_bytes(), original)
            applied = subprocess.run(cmd + ["--write"], capture_output=True, text=True)
            self.assertEqual(applied.returncode, 0, applied.stderr)
            saved = path.read_bytes()
            self.assertIn(b"\r\n", saved)
            self.assertNotIn(b"\n", saved.replace(b"\r\n", b""))
            failed = subprocess.run(cmd[:-1] + ["CAbilityMissing", "--write"], capture_output=True, text=True)
            self.assertEqual(failed.returncode, 1)
            self.assertEqual(path.read_bytes(), saved)


if __name__ == "__main__":
    unittest.main()
