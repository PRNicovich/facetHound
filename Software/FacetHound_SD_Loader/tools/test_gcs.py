"""GCS import tests, runnable directly in Spyder. Optional sample path via CLI."""
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import numpy as np

sys.path.insert(0,str(Path(__file__).resolve().parents[2] / 'gemUtils'))
from gemcad_io import loadGemCADFile
from gem_generator import build_gem
from bulk_convert_gems import find_sources

SAMPLE = '''<?xml version="1.0"?>
<GemCutStudio version="1000">
<!-- editor base must not become an index offset -->
<index gear="96" base="6" symmetry="4" mirror="1"/>
<tier angle="45" depth="1" name="C1" instructions="Meet &amp; polish &#176;">
<facet nx="0.5" ny="-0.5" nz="0.7071067811865476" index_angle="45"/>
</tier>
<tier angle="135" depth="1" name="P1"><facet index_angle="45"/></tier>
<tier angle="90" depth="2" guide="true"><facet index_angle="0"/></tier>
<render refractive_index="1.54"/><info title="Test &amp; Gem" author="A &quot;B&quot;"/>
</GemCutStudio>'''


class GcsTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path=Path(self.temp.name)/'test.GCS'

    def load(self,text):
        self.path.write_text(text,encoding='utf-8')
        return loadGemCADFile(self.path)

    def test_metadata_and_winding(self):
        d=self.load(SAMPLE)
        self.assertEqual(d['boldTitle'],'Test & Gem')
        self.assertEqual(d['littleTitle'],['A "B"'])
        self.assertEqual(d['meridian'],0)
        self.assertEqual(d['foldsymmetry'],0)
        self.assertEqual(len(d['facetList']),2) # construction guide omitted
        c,p=d['facetList']
        self.assertEqual(c['facets'][0]['value'],12)
        self.assertEqual(p['facets'][0]['value'],84)
        self.assertAlmostEqual(p['angle'],-45)
        self.assertEqual(c['comments'],'Meet & polish °')
        self.assertEqual(find_sources(self.path.parent,True),[self.path])

    def test_depth_from_vertices(self):
        d=self.load('''<GemCutStudio version="1000"><index gear="96"/>
        <tier angle="90"><facet nx="0" ny="-1" nz="0">
        <vertex x="0" y="-1" z="0"/><vertex x="1" y="-1" z="0"/>
        <vertex x="0" y="-1" z="1"/></facet></tier></GemCutStudio>''')
        self.assertEqual(d['facetList'][0]['depth'],1)

    def test_bad_inputs(self):
        for text in (SAMPLE[:-20],SAMPLE.replace('gear="96"','gear="0"'),
                     SAMPLE.replace('depth="1"','depth="nan"'),
                     SAMPLE.replace('version="1000"','version="9999"'),
                     '<!DOCTYPE x>'+SAMPLE,SAMPLE.replace('nx="0.5"','nx="nan"')):
            with self.subTest(text=text[:60]),self.assertRaises((ValueError,ET.ParseError)):
                self.load(text)


def check_example(path):
    root=ET.parse(path).getroot()
    source_tiers=[t for t in root.findall('tier') if t.get('guide')!='true']
    native=np.asarray([(-float(v.get('y')),float(v.get('x')),float(v.get('z')))
                       for t in source_tiers for f in t.findall('facet') for v in f.findall('vertex')])
    with contextlib.redirect_stdout(io.StringIO()):
        points,supports,edges,planes,meta,*_=build_gem(path)
    error=np.max(np.min(np.linalg.norm(native[:,None,:]-points[None,:,:],axis=2),axis=1))
    assert error<1e-6,error
    design=loadGemCADFile(path)
    assert len(design['facetList'])==len(source_tiers)
    expected=[f for t in source_tiers for f in t.findall('facet')]
    assert len(planes)==len(expected)
    for (normal,distance),face in zip(planes,expected):
        n=np.array([-float(face.get('ny')),float(face.get('nx')),float(face.get('nz'))])
        np.testing.assert_allclose(normal,n/np.linalg.norm(n),atol=1e-7)
    print(f'{path.name}: {len(source_tiers)} tiers, {len(planes)} facets, {len(points)} vertices, '
          f'{len(edges)} edges; native geometry error {error:.3g}')


if __name__=='__main__':
    result=unittest.main(argv=[__file__],exit=False).result
    if not result.wasSuccessful():
        raise SystemExit(1)
    if len(sys.argv)>1:
        check_example(Path(sys.argv[1]))
