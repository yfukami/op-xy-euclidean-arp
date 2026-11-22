# Euclidean Arp

An Open-XY note player for the OP-XY (OS 1.2.1). It arpeggiates held keys through a Euclidean rhythm and shows the pattern on a 16-slot ring. It targets SDK 1.16, the first version that gives players a two-page header.

## Controls

Press any encoder to switch pages.

| Encoder | Page 1 | Page 2 |
|---|---|---|
| 1 | RATE: 1/2t, 1/4, 1/4t, 1/8, 1/8t, 1/16, 1/16t, 1/32, 1/32t | STEPS: 1 to 16 |
| 2 | STYLE: UP, DOWN, UP/DN, DN/UP, ORDER, RAND, CHORD | HITS: 0 to STEPS |
| 3 | OCTAVE: 1 to 4 | ROTATE: 0 to STEPS - 1 |
| 4 | GATE: 10% to 100% | CHANCE: 0% to 100%, then 95%* to 0%* |

- HITS and ROTATE stop at their limit for the current STEPS. Turning past it has no effect, and the stored value returns if STEPS grows.
- The arpeggio moves to its next note on hits only.
- **First note:** keys that arrive together, such as a sequenced chord, start on the next player step, and STYLE decides which note plays first. A key that arrives on its own waits about 15 ms for the rest of a hand-played chord, so the first note is chosen from every key in it and nothing is struck twice. The steps still count from the first key.
- CHANCE is the probability that each hit plays. Skipped hits still move the arpeggio to its next note. Values marked `*` always play the first hit of each loop.
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

# Chain Arp

A second player in this repository, in `machines/chained_arp/` (ID `chained_arp`). Four arpeggiator slots play one after another in a chain, 1 → 2 → 3 → 4 → 1. Each slot plays its own pattern for CYCLES complete cycles, then hands over to the next slot.

## Controls

Press any encoder to switch pages.

| Encoder | Page 1: slot patterns | Page 2: global settings |
|---|---|---|
| 1 | ARP 1 | RATE: 1/2t to 1/32t, triplets included |
| 2 | ARP 2 | GATE: 10% to 100% |
| 3 | ARP 3 | OCTAVE: 1 to 4 |
| 4 | ARP 4 | CYCLES: 1 to 8, then 1L to 8L (latch on) |

## Patterns

Patterns work on the held notes, sorted low to high and repeated across OCTAVE. N is the number of notes in that list. The examples use C E G B held, with OCTAVE 1 (N = 4).

| Label | Behaviour | Cycle length (steps) | Example |
|---|---|---|---|
| off | The chain skips this slot | — | — |
| up | 1..N | N | C E G B |
| down | N..1 | N | B G E C |
| up/dn | 1..N..2, ends not repeated | 2N−2 | C E G B G E |
| dn/up | N..1..N−1, ends not repeated | 2N−2 | B G E C E G |
| updn+ | 1..N then N..1, ends repeated | 2N | C E G B B G E C |
| dnup+ | N..1 then 1..N, ends repeated | 2N | B G E C C E G B |
| order | Notes in the order they were pressed, repeated per octave | N | (press order) |
| conv | Outside in: 1, N, 2, N−1, ... | N | C B E G |
| divrg | Inside out: CONV reversed | N | G E B C |
| pinky | Top note alternating with each other note: 1,N,2,N,...,N−1,N | 2(N−1) | C B E B G B |
| thumb | Bottom note alternating with each other note: 1,2,1,3,...,1,N | 2(N−1) | C E C G C B |
| stu+ | Groups of 3, rising, each starting one note higher (the original STAIR) | 3(N−2) | C E G, E G B |
| stu- | Groups of 3, falling, each starting one note higher | 3(N−2) | G E C, B G E |
| std- | Groups of 3, falling, each starting one note lower | 3(N−2) | B G E, G E C |
| std+ | Groups of 3, rising, each starting one note lower | 3(N−2) | E G B, C E G |
| shuf | Random order, each note once per cycle, never starting on the note that ended the last | N | (random) |
| rand | A random note each step, repeats allowed | N | (random) |
| drunk | A random walk up or down one note, turning back at the ends and carrying over between cycles | N | (random) |
| chord | All held notes together each step, moving up an octave per hit across OCTAVE | N | CEGB ×4 |
| chd/1 | One chord hit, then up through 1..N | 1 + N | CEGB C E G B |

- **Chords on a monophonic instrument:** CHORD and CHD/1 start each chord's notes highest first and lowest last, so an instrument that can play only one note keeps the lowest note. The whole chord comes from the lowest key, so releasing that key stops the chord until the next step.
- **One note held:** every pattern plays that note, with a cycle length of 1.
- **Two notes held:** `stu+` and `stu-` play as UP, `std-` and `std+` as DOWN. PINKY, THUMB, UP/DN and DN/UP last 2 steps.

## The chain

- **Order:** slots play in order and OFF slots are skipped. If all four are OFF, notes pass straight through to the instrument.
- **Handover:** a slot hands over after CYCLES complete cycles of its pattern. Changing a playing slot's pattern applies from its next cycle.
- **Changing notes:** if the held notes change mid-slot, the note list rebuilds at once and the step continues, wrapping into a shorter cycle if needed.
- **New phrase:** after you release every key (with latch off), the next key starts again at the first slot that isn't OFF.
- **Latch (CYCLES 1L to 8L):** releasing all keys keeps the arp running on the last notes held. Keys pressed after that replace the set and start a new phrase, and keys pressed while others are held join the set. Turning latch off releases the latched notes.
- **Latch limit:** latch keeps up to 7 keys, so a new key always has room. If eight are held, the first one released is let go.

## Screen

- **Bars** under the header columns mark the playing slot.
- **Cycle squares** count the slot's cycles. When stopped, they show CYCLES.
- **Cells** fill as the current cycle plays.
- **Text line:** the slot and note, such as `arp 2  C4`, or `chord`.

## Development

```sh
../open-xy/.venv/bin/python ../open-xy/sdk.py test --machine machines/chained_arp --cxx /opt/homebrew/opt/llvm/bin/clang++
../open-xy/.venv/bin/python ../open-xy/sdk.py test --machine machines/chained_arp --cxx /opt/homebrew/opt/llvm/bin/clang++ --firmware
```

To regenerate the artwork, run `python3 tools/gen_chain_panel.py`. To review it, run `python3 tools/gen_chain_panel.py --state 1,4,1,7 --out /tmp/chain.svg`. Device checks are in `QA.md` under "Chain Arp".

## License

MIT. See [LICENSE](LICENSE).
