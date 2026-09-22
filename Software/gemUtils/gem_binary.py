"""Bounded native GemCAD polygon reader; also usable from Spyder."""
import math
import struct
from pathlib import Path


def load_binary_gem(path):
    data = Path(path).read_bytes()
    if not 40 <= len(data) <= 2 * 1024 * 1024:
        raise ValueError("Unsupported GEM size")
    pos = 0

    def read(fmt):
        nonlocal pos
        size = struct.calcsize('<' + fmt)
        if pos + size > len(data):
            raise ValueError("Truncated GEM")
        result = struct.unpack_from('<' + fmt, data, pos)
        pos += size
        return result

    def text():
        nonlocal pos
        n, = read('B')
        if pos+n > len(data):
            raise ValueError("Truncated GEM text")
        value = data[pos:pos+n].decode('cp1252', errors='replace')
        pos += n
        return value

    tiers = {}
    cut_count = 0
    while pos + 40 <= len(data):
        start = pos
        zero, unknown, folds, mirror, gear, ri, reserved, meridian = read('IIIIidId')
        if (zero == 0 and 0 < folds <= 400 and mirror <= 1 and
                0 < abs(gear) <= 400 and 1 <= ri <= 10 and abs(meridian) <= 400):
            headers, notes = [], []
            footnotes = False
            while pos < len(data):
                value = text()
                if value.lower() == 'preform':
                    raise ValueError('GEM preform sections not supported; export ASC')
                if not value.strip():
                    footnotes = True
                else:
                    (notes if footnotes else headers).append(value)
            if not tiers:
                raise ValueError('No facets')
            for tier in tiers.values():
                for facet in tier['facets']:
                    value = (facet.pop('_turn') * gear + meridian) % abs(gear)
                    if abs(value-round(value)) < 1e-7:
                        value = float(round(value))
                    facet.update(value=value, deg=360*value/abs(gear), frac=value/abs(gear))
            return dict(gemCad_version='GEM binary', wheelIndex=gear, meridian=meridian,
                        foldsymmetry=folds, mirrorPlane=bool(mirror), refractiveIndex=ri,
                        boldTitle=headers[0] if headers else Path(path).stem,
                        littleTitle=headers[1:], comments=notes, facetList=list(tiers.values()))
        pos = start
        nx, ny, nz, tier_id = read('dddI')
        length = math.sqrt(nx*nx+ny*ny+nz*nz)
        if not math.isfinite(length) or length < 1e-12 or not 1 <= tier_id <= 2048:
            raise ValueError('Invalid GEM facet normal/tier')
        normal = (nx/length, ny/length, nz/length)
        label = text().split('\t', 1)
        marker, = read('I')
        vertices = []
        while marker == 1:
            x, y, z, marker = read('dddI')
            if not all(math.isfinite(v) for v in (x,y,z)) or len(vertices) >= 4096:
                raise ValueError('Invalid GEM polygon')
            vertices.append((x,y,z))
        if marker or len(vertices) < 3:
            raise ValueError('Invalid GEM polygon marker')
        distances = [sum(a*b for a,b in zip(normal,v)) for v in vertices]
        distance = distances[0]
        if distance <= 0 or any(not math.isfinite(d) or abs(d-distance)>1e-6*max(1,abs(distance)) for d in distances):
            raise ValueError('Nonplanar GEM polygon')
        angle = math.copysign(math.degrees(math.atan2(math.hypot(nx,ny),abs(nz))),nz)
        tier = tiers.setdefault(tier_id, dict(angle=angle, depth=distance, isCrown=nz>=0,
                                              comments='', facets=[], nFacets=0))
        if len(label)>1 and not tier['comments']:
            tier['comments'] = label[1][:96]
        tier['facets'].append(dict(name=label[0].strip(), _turn=math.atan2(nx,ny)/(2*math.pi)))
        tier['nFacets'] += 1
        cut_count += 1
        if cut_count > 2048:
            raise ValueError('Too many GEM facets')
    raise ValueError('Missing GEM trailer')
