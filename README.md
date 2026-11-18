# Euclidean Arp

An Open-XY note player for the OP-XY (OS 1.2.1). It arpeggiates held keys through a Euclidean rhythm and shows the pattern on a 16-slot ring. It targets SDK 1.16, the first version that gives players a two-page header.

## Controls

Press any encoder to switch pages.

| Encoder | Page 1 | Page 2 |
|---|---|---|
| 1 | RATE: 1/4, 1/8, 1/16, 1/32 | STEPS: 1 to 16 |
| 2 | STYLE: UP, DOWN, UP/DN, ORDER, RAND, CHORD | HITS: 0 to STEPS |
| 3 | OCTAVE: 1 to 4 | ROTATE: 0 to STEPS - 1 |
| 4 | GATE: 10% to 100% | ACCENT: -100% to +100% |

- HITS and ROTATE stop at their limit for the current STEPS. Turning past it has no effect, and the stored value returns if STEPS grows.
- The arpeggio moves to its next note on hits only.
- ACCENT changes the velocity of the first hit played in each pattern cycle.
- CHORD plays every held key together and moves up one octave per hit, across OCTAVE octaves.

## Screen

Large dots are hits and small dots are rests. The inner tick is the playhead. The centre shows the current note, or hits/steps when no key is held.

Step `i` always sits in slot `i`, because a player's dots have fixed positions. With STEPS below 16, the remaining slots look like rests, and the playhead wraps early.

## Development

Run from this directory, with the SDK at `../open-xy`:

```sh
../open-xy/.venv/bin/python ../open-xy/sdk.py test --machine machines/euclidean_arp --cxx /opt/homebrew/opt/llvm/bin/clang++
../open-xy/.venv/bin/python ../open-xy/sdk.py test --machine machines/euclidean_arp --cxx /opt/homebrew/opt/llvm/bin/clang++ --firmware
```

`sdk.py preview` does not support note players. To review the screen, write an SVG and open it:

```sh
python3 tools/gen_panel.py --state 5,8,3 --note C#4 --out /tmp/ring.svg
```

Running `python3 tools/gen_panel.py` with no arguments regenerates `panel.svg.fragment`.

Device checks are in [QA.md](QA.md).
