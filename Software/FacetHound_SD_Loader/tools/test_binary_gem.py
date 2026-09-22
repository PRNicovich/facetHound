"""Native GEM regressions. Run directly in Spyder; no hardware required."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'gemUtils'))
from gemcad_io import loadGemCADFile
from bulk_convert_gems import find_sources


def fixture(gear=96, meridian=0):
    label = b'A\tCut this tier'
    polygon = struct.pack('<dddIB', 1, 0, 0, 1, len(label)) + label
    polygon += struct.pack('<I', 1)
    for vertex, marker in [((1, 0, 0), 1), ((1, 1, 0), 1), ((1, 0, 1), 0)]:
        polygon += struct.pack('<dddI', *vertex, marker)
    trailer = struct.pack('<IIIIidId', 0, 0, 4, 1, gear, 1.54, 0, meridian)
    return polygon + trailer + b'\x04Test\x00'


class BinaryGemTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'sample.GEM'

    def load(self, data):
        self.path.write_bytes(data)
        return loadGemCADFile(str(self.path))

    def test_metadata_and_coordinates(self):
        design = self.load(fixture())
        tier = design['facetList'][0]
        self.assertEqual(design['boldTitle'], 'Test')
        self.assertEqual(tier['comments'], 'Cut this tier')
        self.assertEqual(tier['facets'][0]['name'], 'A')
        self.assertEqual(tier['facets'][0]['value'], 24)
        self.assertEqual(tier['angle'], 90)
        self.assertEqual(tier['depth'], 1)

    def test_signed_gear_and_meridian(self):
        design = self.load(fixture(-96, 2.5))
        self.assertEqual(design['facetList'][0]['facets'][0]['value'], 74.5)

    def test_truncation_and_invalid_normal(self):
        data = fixture()
        for length in range(len(data)-6):
            with self.subTest(length=length), self.assertRaises(ValueError):
                self.load(data[:length])
        with self.assertRaises(ValueError):
            self.load(struct.pack('<d', float('nan')) + data[8:])

    def test_preform_rejected(self):
        with self.assertRaisesRegex(ValueError, 'preform'):
            self.load(fixture() + b'\x07preform')

    def test_nested_source_discovery(self):
        self.load(fixture())
        nested = self.path.parent / 'Crown'
        nested.mkdir()
        (nested / 'other.ASC').write_text('GemCad 5.0')
        (nested / 'notes.txt').write_text('not a gem')
        self.assertEqual(len(find_sources(self.path.parent, False)), 1)
        self.assertEqual(len(find_sources(self.path.parent, True)), 2)


if __name__ == '__main__':
    unittest.main(argv=[__file__])
