#!/usr/bin/env python3
"""Composite an extracted cursor over a retail framebuffer pair and compare RGB.

Requires built mpqtool/mdxtool, ImageMagick, and a working SDL display. Uses the
production sprite draw path. The capture's scene background is retained exactly;
this isolates cursor rendering from unrelated world-renderer differences.
"""
import argparse
import json
import struct
import subprocess
from pathlib import Path

from verify_wc3_cursor_trace import sequences


def read_capture(folder, stage):
    info = json.loads((folder / (stage + '.json')).read_text())
    width, height, pitch = info['width'], info['height'], info['pitch']
    if info['format'] not in (21, 22) or min(width, height) <= 0 or pitch < width * 4:
        raise ValueError('expected a positive-size BGRA framebuffer')
    data = (folder / (stage + '.bgra')).read_bytes()
    if len(data) != height * pitch:
        raise ValueError('framebuffer byte count does not match metadata')
    bgr = b''.join(data[y*pitch+x*4:y*pitch+x*4+3]
                   for y in range(height) for x in range(width))
    return info, bgr


def compare(args):
    folder = args.capture.resolve()
    before, background = read_capture(folder, 'before')
    after, retail = read_capture(folder, 'after')
    for field in ('width', 'height', 'position', 'animIndex', 'animFrame'):
        if before[field] != after[field]:
            raise ValueError('cursor changed across draw capture: ' + field)
    width, height = after['width'], after['height']
    seqs = sequences(args.model)
    name, begin, end = seqs[after['animIndex']]
    frame = after['animFrame']
    if not begin <= frame <= end:
        raise ValueError('captured frame lies outside the supplied model sequence')
    changed = sum(a != b for a, b in zip(background, retail))
    if not changed:
        raise ValueError('capture contains no visible cursor contribution')
    tga = folder / 'comparison-background.tga'
    tga.write_bytes(struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0,
                               width, height, 24, 32) + background)
    archive = folder / 'comparison.mpq'
    model_name = args.model.name
    pack = [str(args.bin_dir / 'mpqtool'), '-mpq', str(archive), 'pack',
            str(args.model.resolve()), model_name, str(tga), 'CursorBackground.tga']
    for source, member in args.asset:
        pack.extend((str(Path(source).resolve()), member))
    subprocess.run(pack, check=True)
    output = folder / 'comparison-engine.png'
    # Retail uses .8 x .6 with top-origin input; account for D3D9 pixel centers.
    x = after['position'][0] + .4 / width
    y = .6 - after['position'][1] + .3 / height
    render = [str(args.bin_dir / 'mdxtool'), '-mpq', str(archive), '-model', model_name,
              '--sprite', str(x), str(y), '--size', str(width), str(height),
              '--anim', name, '--frame', str(frame-begin),
              '--background', 'CursorBackground.tga', '-o', str(output)]
    with (folder / 'comparison-render.log').open('w') as log:
        subprocess.run(render, check=True, stdout=log, stderr=log)
    engine = subprocess.check_output(['magick', str(output), '-depth', '8', 'bgr:-'])
    if len(engine) != len(retail):
        raise ValueError('engine framebuffer dimensions differ from retail')
    differences = [abs(a-b) for a, b in zip(retail, engine)]
    result = dict(sequence=name, authoredFrame=frame, width=width, height=height,
                  cursorChangedComponents=changed,
                  differingComponents=sum(d != 0 for d in differences),
                  absoluteSum=sum(differences), maximum=max(differences),
                  renderCommand=render)
    (folder / 'comparison-result.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--model', type=Path, required=True, help='extracted MDX used by retail')
    parser.add_argument('--asset', nargs=2, action='append', default=[], metavar=('FILE', 'MPQ_NAME'))
    parser.add_argument('--bin-dir', type=Path, default=Path(__file__).resolve().parents[2] / 'build/bin')
    args = parser.parse_args()
    print(json.dumps(compare(args), indent=2))


if __name__ == '__main__':
    main()
