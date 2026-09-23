"""Native GEM regressions. Run directly in Spyder; no hardware required."""
from pathlib import Path
import struct
import re
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
    def test_firmware_footer_avoids_arduino_word_macro(self):
        source = (Path(__file__).resolve().parents[1] /
                  'baseChassisModule/gemBinaryReader.h').read_text()
        source = re.sub(r'//[^\n]*', '', source)
        self.assertNotRegex(source, r'\bword\s*\(')
        for offset in (0, 8, 12, 16):
            self.assertIn(f'readLe32({offset})', source)

    def test_reported_tessellation_footer(self):
        footer = bytes.fromhex(
            '00000000F069F8C0040000000100000060000000'
            'E17A14AE47E1F63FFF7F00000000000000000000')
        zero, _, folds, mirror, gear, ri, _, meridian = struct.unpack('<IIIIidId', footer)
        self.assertEqual((zero, folds, mirror, gear, meridian), (0, 4, 1, 96, 0))
        self.assertAlmostEqual(ri, 1.43)
        data = fixture()
        self.assertEqual(self.load(data[:-46] + footer + b'\x04Test\x00')['boldTitle'], 'Test')

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
