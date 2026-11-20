#!/usr/bin/env python3
"""Generates machines/euclidean_arp/panel.svg.fragment, or a standalone review SVG.

  python3 tools/gen_panel.py
  python3 tools/gen_panel.py --state 5,8,3 --note C#4 --out /path/to/review.svg

--state is hits,steps,playhead (playhead -1 when no key is held). It uses the
same pattern and dot rules as display() in player.cpp, with ROTATE 0.
"""
import argparse
import math
from pathlib import Path

PREFIX = 'euclid_arp'
OUTPUT = Path(__file__).resolve().parents[1] / 'machines/euclidean_arp/panel.svg.fragment'
CX, CY = 240, 130
RING, HIT_R, REST_R = 74, 6, 2.5
TICK_IN, TICK_OUT, TICK_HALF = 53, 63, 2.5
COVER_IN, COVER_OUT = 49, 66
SLOTS = 16


def n(value):
    text = f'{value:.3f}'.rstrip('0').rstrip('.')
    return '0' if text in ('-0', '') else text


def polar(radius, degrees):
    angle = math.radians(degrees)
    return radius * math.sin(angle), -radius * math.cos(angle)


def slot_angle(slot):
    return slot * 360 / SLOTS


def paint(colour):
    return f'fill="{colour}" stroke="{colour}" stroke-width="1"'


def at(x, y):
    return f'transform="matrix(1 0 0 1 {n(x)} {n(y)})"'


def outline(points):
    return 'M' + ' L'.join(f'{n(x)} {n(y)}' for x, y in points) + ' Z'


# Every shape is a path of straight lines: the OP-XY drew none of the <circle>
# elements an earlier version used, while these paths and the text showed.
def disc(radius, sides):
    return outline([polar(radius, i * 360 / sides) for i in range(sides)])


def ring(radius, width, segments=64, gap=0.8):
    """A thin circle as separate short quads, so no two subpaths share a point."""
    parts = []
    for i in range(segments):
        a, b = i * 360 / segments, (i + 1) * 360 / segments - gap
        parts.append(outline([polar(radius - width / 2, a), polar(radius + width / 2, a),
                              polar(radius + width / 2, b), polar(radius - width / 2, b)]))
    return ' '.join(parts)


def tick(slot):
    a = slot_angle(slot)
    side = a + 90
    corners = []
    for radius, sign in ((TICK_IN, -1), (TICK_OUT, -1), (TICK_OUT, 1), (TICK_IN, 1)):
        x, y = polar(radius, a)
        dx, dy = polar(TICK_HALF * sign, side)
        corners.append((x + dx, y + dy))
    return outline(corners)


def cover(quadrant):
    start = slot_angle(4 * quadrant) - 360 / SLOTS / 2
    angles = [start + i * 90 / 12 for i in range(13)]
    points = [polar(COVER_OUT, a) for a in angles] + [polar(COVER_IN, a) for a in reversed(angles)]
    return outline(points)


def dots():
    """The 24 bound dots, in dot order: (index, svg). Paint order is ticks before covers."""
    result = []
    for slot in range(SLOTS):
        x, y = polar(RING, slot_angle(slot))
        result.append((slot, f'<path id="{PREFIX}_dot_{slot}" {at(CX + x, CY + y)} d="{disc(HIT_R, 16)}" {paint("#FFFFFF")}/>'))
    for r in range(4):
        d = ' '.join(tick(r + 4 * q) for q in range(4))
        result.append((16 + r, f'<path id="{PREFIX}_dot_{16 + r}" {at(CX, CY)} d="{d}" {paint("#FFFFFF")}/>'))
    for q in range(4):
        result.append((20 + q, f'<path id="{PREFIX}_dot_{20 + q}" {at(CX, CY)} d="{cover(q)}" {paint("#000000")}/>'))
    return result


def static_art():
    art = [f'<path {at(CX, CY)} d="{ring(RING, 1.2)}" fill="#4A4A4A" stroke="#4A4A4A" stroke-width="0.2"/>']
    for slot in range(SLOTS):
        x, y = polar(RING, slot_angle(slot))
        art.append(f'<path {at(CX + x, CY + y)} d="{disc(REST_R, 8)}" {paint("#888888")}/>')
    return art


def text_node(content):
    return (f'<text id="{PREFIX}_text" {at(CX, CY + 9)} fill="#FFFFFF" text-anchor="middle" '
            f'font-family="UniversNextPro-Light" font-size="26">{content}</text>')


def fragment():
    body = static_art() + [svg for _, svg in dots()] + [text_node('5/8')]
    return f'<g id="{PREFIX}_panel">\n' + '\n'.join(body) + '\n</g>\n'


def lit_mask(hits, steps, playhead):
    mask = 0
    for i in range(steps):
        if hits and (i * hits) % steps < hits:
            mask |= 1 << i
    if playhead >= 0:
        mask |= 1 << (16 + playhead % 4)
        for q in range(4):
            if q != playhead // 4:
                mask |= 1 << (20 + q)
    return mask


def review(hits, steps, playhead, note):
    mask = lit_mask(hits, steps, playhead)
    shown = [svg for index, svg in dots() if mask >> index & 1]
    label = note if playhead >= 0 else f'{hits}/{steps}'
    body = '\n'.join(static_art() + shown + [text_node(label)])
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="960" height="444" viewBox="0 0 480 222">\n'
            '<rect width="480" height="222" fill="#000000"/>\n'
            '<rect width="480" height="38" fill="#1C1C1C"/>\n' + body + '\n</svg>\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--state', help='hits,steps,playhead')
    parser.add_argument('--note', default='C4')
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    if args.state:
        hits, steps, playhead = (int(part) for part in args.state.split(','))
        parser.error('--state needs --out') if not args.out else None
        args.out.write_text(review(hits, steps, playhead, args.note), encoding='utf-8')
    else:
        (args.out or OUTPUT).write_text(fragment(), encoding='utf-8')


if __name__ == '__main__':
    main()
