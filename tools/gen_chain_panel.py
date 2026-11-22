#!/usr/bin/env python3
"""Generates machines/chained_arp/panel.svg.fragment, or a standalone review SVG.

  python3 tools/gen_chain_panel.py
  python3 tools/gen_chain_panel.py --state 1,2,4,7 --text "arp 2  C4" --out /path/to/review.svg

Dots 0..3 are bars under the four header columns, marking the playing slot;
dots 4..11 count the slot's cycles; dots 12..23 fill as the current cycle
plays. --state is slot,cycles,cycle,filled (slot -1 for none), as display()
in player.cpp shows them. Everything is drawn with straight-line <path>
elements, because the OP-XY did not draw <circle>, and stays below the header (y = 38).
"""
import argparse
from pathlib import Path

PREFIX = 'chained_arp'
OUTPUT = Path(__file__).resolve().parents[1] / 'machines/chained_arp/panel.svg.fragment'
CYCLE_X0, CYCLE_DX, CYCLE_Y, CYCLE_W = 129, 30, 96, 18       # 8 squares, centred
PROGRESS_X0, PROGRESS_DX, PROGRESS_Y, PROGRESS_W = 72, 29, 134, 22  # 12 cells, centred


def n(value):
    text = f'{value:.3f}'.rstrip('0').rstrip('.')
    return '0' if text in ('-0', '') else text


def rect(w, h):
    return f'M0 0 L{n(w)} 0 L{n(w)} {n(h)} L0 {n(h)} Z'


def node(x, y, d, colour, name=None):
    ident = f'id="{PREFIX}_dot_{name}" ' if name is not None else ''
    return (f'<path {ident}transform="matrix(1 0 0 1 {n(x)} {n(y)})" d="{d}" '
            f'fill="{colour}" stroke="{colour}" stroke-width="1"/>')


def dots():
    """The 24 bound dots, in dot order: (index, svg)."""
    result = [(i, node(12 + 120 * i, 42, rect(96, 4), '#FFFFFF', i)) for i in range(4)]
    result += [(4 + i, node(CYCLE_X0 + CYCLE_DX * i, CYCLE_Y, rect(CYCLE_W, CYCLE_W), '#FFFFFF', 4 + i)) for i in range(8)]
    result += [(12 + i, node(PROGRESS_X0 + PROGRESS_DX * i, PROGRESS_Y, rect(PROGRESS_W, 14), '#FFFFFF', 12 + i))
               for i in range(12)]
    return result


def static_art():
    """Outlines under the cycle squares and progress cells, so empty ones still show."""
    art = [node(CYCLE_X0 + CYCLE_DX * i, CYCLE_Y + CYCLE_W + 3, rect(CYCLE_W, 2), '#666666') for i in range(8)]
    art += [node(PROGRESS_X0 + PROGRESS_DX * i, PROGRESS_Y + 17, rect(PROGRESS_W, 2), '#666666') for i in range(12)]
    art.append('<text transform="matrix(1 0 0 1 44 111)" fill="#8A8A8A" font-family="UniversNextPro-Light" '
               'font-size="13">cycle</text>')
    return art


def text_node(content):
    return (f'<text id="{PREFIX}_text" transform="matrix(1 0 0 1 240 190)" fill="#FFFFFF" '
            f'text-anchor="middle" font-family="UniversNextPro-Light" font-size="26">{content}</text>')


def fragment():
    body = static_art() + [svg for _, svg in dots()] + [text_node('ready')]
    return f'<g id="{PREFIX}_panel">\n' + '\n'.join(body) + '\n</g>\n'


def review(slot, cycles, cycle, filled, content):
    lit = set()
    if slot >= 0:
        lit.add(slot)
    lit |= {4 + i for i in range(min(cycle + 1, cycles))}
    lit |= {12 + i for i in range(filled)}
    body = '\n'.join(static_art() + [svg for i, svg in dots() if i in lit] + [text_node(content)])
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="960" height="444" viewBox="0 0 480 222">\n'
            '<rect width="480" height="222" fill="#000000"/>\n<rect width="480" height="38" fill="#1C1C1C"/>\n'
            + body + '\n</svg>\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--state', help='slot,cycles,cycle,filled')
    parser.add_argument('--text', default='arp 2  C4')
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    if args.state:
        if not args.out:
            parser.error('--state needs --out')
        slot, cycles, cycle, filled = (int(part) for part in args.state.split(','))
        args.out.write_text(review(slot, cycles, cycle, filled, args.text), encoding='utf-8')
    else:
        (args.out or OUTPUT).write_text(fragment(), encoding='utf-8')


if __name__ == '__main__':
    main()
