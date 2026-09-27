"""Guards tools/galaxy_audit.py placeholder detection against the live Galaxy host sources."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import galaxy_audit  # noqa: E402


class GalaxyAuditPlaceholders(unittest.TestCase):
    def test_known_header_stubs_are_placeholders(self):
        found = galaxy_audit.placeholders()
        self.assertIn('sc2_SoundStop', found)  # `{ (void)j; return jass_pushnull(j); }` in galaxy_sound.h
        self.assertIn('sc2_RegionPlayableMapSet', found)  # stub without `(void)j;`
        self.assertEqual(galaxy_audit.registry()['SoundStop'], 'sc2_SoundStop')

    def test_pointer_spacing_does_not_matter(self):
        for signature in ['jass_t *j', 'jass_t * j', 'jass_t* j', 'jass_t*j']:
            source = f'static uint32_t sc2_X({signature}) {{ (void)j; return jass_pushnull(j); }}'
            self.assertEqual(galaxy_audit.PLACEHOLDER.findall(source), ['sc2_X'], signature)

    def test_real_binding_is_not_a_placeholder(self):
        source = 'static uint32_t sc2_X(jass_t *j) { return jass_pushinteger(j, jass_checkinteger(j, 1)); }'
        self.assertEqual(galaxy_audit.PLACEHOLDER.findall(source), [])


if __name__ == '__main__':
    unittest.main()
