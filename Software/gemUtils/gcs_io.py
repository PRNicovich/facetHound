"""Gem Cut Studio XML import, normalized to Facet Hound's ASC coordinates."""
import math
from pathlib import Path
import xml.etree.ElementTree as ET


def load_gcs(path):
    data = Path(path).read_bytes()
    if len(data) > 2*1024*1024 or b'<!DOCTYPE' in data or b'<!ENTITY' in data:
        raise ValueError('Unsupported GCS size or XML declarations')
    root = ET.fromstring(data)
    if root.tag != 'GemCutStudio' or root.get('version') != '1000':
        raise ValueError('Unsupported GCS version')

    def number(element, key, default=None):
        value = element.get(key)
        if value is None:
            if default is not None:
                return default
            raise ValueError('Missing GCS '+key)
        result = float(value)
        if not math.isfinite(result):
            raise ValueError('Invalid GCS '+key)
        return result

    index = root.find('index')
    if index is None:
        raise ValueError('Missing GCS index gear')
    gear = number(index, 'gear')
    if not 1 <= gear <= 400:
        raise ValueError('Unsupported GCS gear')
    tiers = []
    count = 0
    for source in root.findall('tier'):
        if source.get('guide', 'false').lower() == 'true':
            continue
        angle = number(source, 'angle')
        if not 0 <= angle <= 180:
            raise ValueError('Invalid GCS tier angle')
        tier = dict(comments=source.get('instructions', '')[:96], facets=[], nFacets=0)
        for facet in source.findall('facet'):
            ia = number(facet, 'index_angle', 0.0)
            if any(key in facet.attrib for key in ('nx', 'ny', 'nz')):
                normal = [number(facet, key) for key in ('nx', 'ny', 'nz')]
            else:
                if 'index_angle' not in facet.attrib:
                    raise ValueError('GCS facet needs a normal or index_angle')
                t, az = math.radians(angle), math.radians(ia)
                normal = [math.sin(t)*math.sin(az)*(1 if angle<90 else -1),
                          -math.sin(t)*math.cos(az), math.cos(t)]
            length = math.sqrt(sum(v*v for v in normal))
            if not math.isfinite(length) or length < 1e-12:
                raise ValueError('Invalid GCS normal')
            nx, ny, nz = [v/length for v in normal]
            vertices = facet.findall('vertex')
            if len(vertices)>4096:
                raise ValueError('Too many GCS vertices')
            distances = [nx*number(v,'x')+ny*number(v,'y')+nz*number(v,'z') for v in vertices]
            distance = number(source,'depth', distances[0] if distances else None)
            if not math.isfinite(distance) or distance<=0 or any(
                    not math.isfinite(d) or abs(d-distance)>1e-6*max(1,abs(distance)) for d in distances):
                raise ValueError('Invalid/nonplanar GCS facet')
            tilt = math.copysign(math.degrees(math.atan2(math.hypot(nx,ny),abs(nz))),nz)
            # Rotate native XY by +90 degrees: (x,y,z) -> (-y,x,z).
            azimuth = math.atan2(nx,-ny) if math.hypot(nx,ny)>1e-10 else math.radians(ia)
            value = (azimuth*gear/(2*math.pi)) % gear
            if abs(value-round(value))<1e-7:
                value = float(round(value)) % gear
            if not tier['facets']:
                tier.update(angle=tilt, depth=distance, isCrown=nz>=0)
            elif abs(tilt-tier['angle'])>1e-6 or abs(distance-tier['depth'])>1e-6:
                raise ValueError('Inconsistent GCS tier planes')
            tier['facets'].append(dict(name=source.get('name',''), value=value,
                                       deg=value*360/gear, frac=value/gear))
            count += 1
            if count>2048:
                raise ValueError('Too many GCS facets')
        tier['nFacets'] = len(tier['facets'])
        if not tier['nFacets']:
            raise ValueError('Empty GCS tier')
        tiers.append(tier)
    if not tiers:
        raise ValueError('No GCS facets')
    info = root.find('info')
    attrs = {} if info is None else info.attrib
    render = root.find('render')
    ri = number(render,'refractive_index',0.0) if render is not None else 0.0
    # GCS base/symmetry/mirror describe editor state, not design symmetry.
    return dict(gemCad_version='GCS 1000', wheelIndex=gear, meridian=0.0,
                foldsymmetry=0, mirrorPlane=False, refractiveIndex=ri,
                boldTitle=attrs.get('title') or Path(path).stem,
                littleTitle=[attrs[k] for k in ('author','header2','header3') if attrs.get(k)],
                comments=[v for k,v in attrs.items() if k.startswith('footer')], facetList=tiers)
