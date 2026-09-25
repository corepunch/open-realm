"""Boundary checks for generated names and include guards."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FdfBindingNames(unittest.TestCase):
    def test_include_guard_keeps_suffix(self):
        for prefix in ['Example', 'a' * 159]:
            result = subprocess.run([str(ROOT / 'build/bin/fdfbindgen'), '-prefix', prefix, '-'],
                                    input='Frame "FRAME" "Root" {}', text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('#ifndef ' + prefix.upper() + '_H\n', result.stdout)

    def test_duplicate_name_overflow_is_an_error(self):
        source = r'''
#define main fdfbindgen_main
#include "tools/fdfbindgen.c"
#undef main
int main(int argc, char **argv) {
    node_count = 2;
    nodes[0].parent = nodes[1].parent = -1;
    nodes[0].first_child = nodes[1].first_child = -1;
    memset(nodes[0].ident, 'a', MAX_IDENT - 1);
    strcpy(nodes[1].ident, nodes[0].ident);
    if (argc > 1) {
        strcpy(nodes[0].binding_ident, nodes[0].ident);
        assign_binding_idents(1);
    } else uniquify_node_ident(1);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            binary = str(Path(directory, 'names'))
            subprocess.run(['cc', '-I', str(ROOT), '-x', 'c', '-', '-o', binary],
                           input=source, text=True, check=True)
            for args in [[], ['binding']]:
                result = subprocess.run([binary, *args], capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 1)
                self.assertIn('identifier too long', result.stderr)


if __name__ == '__main__':
    unittest.main()
