# Device checklist

Run every check on one OP-XY (OS 1.2.1), top to bottom, in one project, on the exact commit you will publish. Host tests model the SDK adapter and stock's chord records, so they cannot answer these questions.

**Setup:** one track with a simple sustaining synth patch, with Euclidean Arp as its player. Start each section from these values, then change only what a check says.

| Page 1 | Page 2 (press any encoder) |
|---|---|
| RATE `1/16` | STEPS `8` |
| STYLE `up` | HITS `5` |
| OCTAVE `1` | ROTATE `0` |
| GATE `50%` | CHANCE `100%` |

Where a check uses a chord, step-sequence C, E, G into step 1 with a note length shorter than the loop.

## Core timing

1. **Clock while stopped.** With the transport stopped, hold one key. Pass: the arp keeps looping its rhythm.
2. **Clock while playing.** STEPS 16, HITS 16. Press play and hold a key against a drum on sixteenths at 80, 120 and 160 BPM. Pass: notes stay locked to the drum.
3. **Short notes, long hold.** GATE `10%`, about 70 BPM, hold three keys for five minutes. Pass: no stray or stuck notes or clicks, and all three keys keep cycling.
4. **Silent pattern.** HITS `0`, hold a key for five minutes, then raise HITS to 5. Pass: silent throughout, then plays at once.

## Notes stop cleanly

5. **Releasing keys one at a time.** GATE `100%`, RATE `1/4`, hold C, E, G and release them one by one while notes sound. Pass: each released key's note stops, the others keep cycling, nothing hangs.
6. **Stopping a sequence.** Sequence notes with GATE `100%` and RATE `1/4`, press play, then stop mid-note. Pass: silence, nothing hangs.
7. **Stop and play while holding a key.** Pass: the arp keeps playing unchanged through play and stop, and nothing hangs after release.
8. **Interruptions while holding two keys.** (a) Switch track. Pass: the held keys keep arpeggiating on their own track until released, and new keys play the new track. (b) Switch project. Pass: silence at once. (c) All-notes-off, where available. Pass: silence, and the next key starts at step 0.
9. **Nine keys.** Pass: eight play, and the ninth has no effect, including on release.

## Controls and screen

10. **Page switching.** Press any encoder. Pass: the header switches pages, and all eight names and values read correctly.
11. **Full range of every control.** Pass: one value per detent. GATE steps `10%` to `100%`. RATE shows `1/2t` to `1/32t`, and STYLE shows `up` to `chord`, without touching the next column. CHANCE runs `0%` to `100%`, then `95%*` to `0%*`.
12. **Limits.** STEPS 16, HITS 12, ROTATE 10, then STEPS 6. Pass: HITS and ROTATE show `6` and `5`, and the ring shows a valid pattern.
13. **Playhead.** STEPS 16, HITS 5, hold a key. Pass: exactly one inner tick shows, visiting all 16 positions in order. The black covers hiding the other ticks are invisible against the black page.
14. **Layout.** Pass: the ring and the centre note sit below the header and read clearly.
15. **Saving.** Set all eight controls to unusual values, save, switch project and back. Pass: all eight return.
16. **Name.** Pass: the player menu shows `euclid_arp` in full.

## Musical features

17. **Styles,** with the chord and STEPS 8, HITS 8. Pass: `up` C E G, `down` G E C, `up/dn` C E G E, `dn/up` G E C E, `order` arrival order, `rand` random notes from the chord, `chord` all three together.
18. **Sequenced first note.** Pass: with `up`, every repeat starts on C, on the step, with no double attack.
19. **Hand-played chords.** Pass: with `up`, usually starts on C. A very quick double attack is acceptable when C lands last.
20. **Octaves.** Pass: OCTAVE `2` plays the chord, then an octave up. At OCTAVE `4`, notes above MIDI 127 drop out.
21. **Rotate.** STEPS 8, HITS 3. Pass: 3 white hit dots, grey rest dots and a thin grey ring show. ROTATE moves the dots and shifts the rhythm, and the note order stays C, E, G.
22. **Chance.** STEPS 8, HITS 8. Pass: `50%` plays about half the notes. `0%*` plays one note per loop, on the first step, advancing through the chord. `0%` is silent.
23. **Triplets.** STEPS 3, HITS 3, drum on quarter notes. Pass: `1/8t` plays three notes per beat and stays locked. `1/4t` plays three notes over two beats.
24. **Long sequenced notes.** The chord's note length fills the loop. Pass: the arp returns to C each loop.

## Extremes

25. **Fastest.** About 200 BPM, `1/32t`, STEPS 16, HITS 16, GATE `10%`, hold three keys. Pass: steady notes, none dropped or stuck, clean stop on release.
26. **Slowest.** About 40 BPM, `1/2t`, GATE `100%`, hold two keys. Pass: notes change on time without overlapping or hanging.
27. **Saved project on a new build.** Reopen the project from check 15. Pass: the arp and all eight values return.
28. **Fresh start.** Release all keys, then press one. Pass: the first note comes at once, and the tick starts at the top of the ring.

## Results

2026-11-19, OS 1.2.1, all 28 checks passed, with check 8c skipped.

- Checks 1–11, 15–20 and 22–24 ran on build `euclid-004`, made from commit `e704c9e`.
- Checks 12–14, 21 and 25–28 ran on build `euclid-005`, made from the code committed as `04d1357`, which draws the ring with paths. Commits after it change documentation only.
- Check 21 failed on `euclid-004`: the ring's `<circle>` elements did not draw on the device. `04d1357` replaced them with paths, which do draw.

