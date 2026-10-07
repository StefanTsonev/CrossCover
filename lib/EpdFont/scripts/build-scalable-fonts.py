#!/usr/bin/env python3
"""Build S3 outline assets from the same source faces/coverage as bitmap fonts.
Requires fonttools. Sources are supplied explicitly; outputs are reproducible.
"""
import argparse
import re
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools import subset
from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.transformPen import TransformPen

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
headers = Path(__file__).resolve().parents[1] / 'builtinFonts'
assets = []
for family, directory in [('bitter', 'Bitter'), ('lexenddeca', 'LexendDeca')]:
    for style in ['Regular', 'Bold', 'Italic', 'BoldItalic']:
        name = f'{family}_{style.lower()}'
        header = (headers / f'{family}_12_{style.lower()}.h').read_text()
        intervals = re.search(r'Intervals\[\] = \{(.*?)\n\};', header, re.S).group(1)
        points = set()
        for a,b in re.findall(r'\{\s*(0x[0-9A-Fa-f]+|\d+),\s*(0x[0-9A-Fa-f]+|\d+),', intervals):
            points.update(range(int(a,0),int(b,0)+1))
        font = TTFont(args.source/directory/f'{directory}-{style}.ttf', recalcTimestamp=False)
        cmap = font.getBestCmap()
        # Preserve the generator's first-covering-face policy. Fallback outlines
        # are decomposed and scaled to the primary em before being appended.
        fallbacks = [args.source/'ChareInk7'/f'ChareInk7-{style}.ttf',
                     args.source/'NotoSymbols'/'NotoSansSymbols-Regular.ttf']
        if style == 'Regular':
            fallbacks.append(args.source/'NotoSansCJKsc'/'NotoSansCJKsc-Regular.otf')
        for fallback_path in fallbacks:
            other = TTFont(fallback_path, recalcTimestamp=False)
            othermap = other.getBestCmap()
            glyphset = other.getGlyphSet()
            factor = font['head'].unitsPerEm / other['head'].unitsPerEm
            for cp in sorted(points-cmap.keys()):
                if cp not in othermap:
                    continue
                glyphname = f'fallback{cp:06x}'
                recording = DecomposingRecordingPen(glyphset)
                glyphset[othermap[cp]].draw(recording)
                pen = TTGlyphPen(None)
                recording.replay(TransformPen(Cu2QuPen(pen,1,reverse_direction=False), (factor,0,0,factor,0,0)))
                glyph = pen.glyph()
                font['glyf'][glyphname] = glyph
                order=font.getGlyphOrder()
                if glyphname not in order: order.append(glyphname)
                font.setGlyphOrder(order)
                advance, bearing = other['hmtx'][othermap[cp]]
                font['hmtx'][glyphname] = (round(advance*factor), round(bearing*factor))
                for table in font['cmap'].tables:
                    if table.isUnicode() and (table.format in (12,13) or cp <= 0xffff):
                        table.cmap[cp]=glyphname
                cmap[cp]=glyphname
            other.close()
        missing=points-font.getBestCmap().keys()
        for cp in sorted(missing):
            # The bitmap format includes explicit zero-width control entries.
            if not re.search(r'\{\s*0,\s*0,\s*0,\s*0,\s*0,\s*0,\s*\d+\s*\}, // U\+'+f'{cp:04X}', header):
                raise RuntimeError(f'{name}: missing visible U+{cp:04X}')
            glyphname=f'empty{cp:06x}'
            font['glyf'][glyphname]=TTGlyphPen(None).glyph()
            order=font.getGlyphOrder()
            if glyphname not in order: order.append(glyphname)
            font.setGlyphOrder(order)
            font['hmtx'][glyphname]=(0,0)
            for table in font['cmap'].tables:
                if table.isUnicode(): table.cmap[cp]=glyphname
        options=subset.Options()
        # Normalize our derived faces to the family/style shown in CrossInk.
        for nid,value in [(1,directory),(2,style),(16,directory),(17,style)]:
            font['name'].setName(value,nid,3,1,0x409)
        font['head'].macStyle=(1 if 'Bold' in style else 0)|(2 if 'Italic' in style else 0)
        font['OS/2'].fsSelection=(32 if 'Bold' in style else 0)|(1 if 'Italic' in style else 0)
        if style=='Regular': font['OS/2'].fsSelection |= 64
        options.name_IDs=[0,1,2,3,4,5,6,13,14,16,17]
        options.hinting=False
        options.recalc_timestamp=False
        options.layout_features=['*']
        sub=subset.Subsetter(options=options)
        sub.populate(unicodes=points)
        sub.subset(font)
        path=args.output/f'{name}.ttf'
        font.save(path)
        assert points <= TTFont(path).getBestCmap().keys()
        assets.append((name,path))
        print(f'{name}: {len(points)} codepoints, {path.stat().st_size} bytes')
# Binary assets are authoritative generated outputs; C arrays are generated at
# build time and intentionally ignored by git.
(args.output/'manifest.txt').write_text('\n'.join(name+'.ttf' for name,_ in assets)+'\n')
