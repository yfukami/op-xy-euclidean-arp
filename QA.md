# Device checklist

None of this has been run on hardware. Host tests use a model of the SDK adapter and of stock's chord records, so they cannot answer these questions. Run the checks in order.

## Blocking unknowns

1. **Clock while stopped.** With the transport stopped, hold one key, with HITS above 1. Pass: the arpeggio keeps running. If only the first note plays, player steps do not run while stopped, and the machine needs a design change.
2. **Clock while playing.** Press play and hold a key at 1/16, STEPS 16, HITS 16. Pass: notes land on sixteenths at several tempos.
3. **Silent held keys.** Set GATE to 10% and hold three keys for at least five minutes at a low tempo. Pass: no stray notes, stuck notes or clicks, and the arpeggio never drops a key.
4. **HITS 0.** Hold a key for at least five minutes with HITS 0, then raise HITS. Pass: silent throughout, then plays at once.

## Note cleanup

5. Release keys one at a time during long gates. Pass: each key's own note stops, and no note hangs.
6. Record a sequence through the player, then press stop mid-note. Pass: no note hangs.
7. Hold a key live and press stop and play. Record what happens, then confirm no note hangs after release.
8. Change track, change project and trigger all-notes-off while holding keys. Pass: silence, and the next key press starts at step 0.
9. Hold nine keys. Pass: eight play, and the ninth has no effect, including when it is released.

## Controls and screen

10. Press an encoder. Pass: the two-page header switches, and all eight values read correctly.
11. Turn each encoder through its full range. Pass: one value per detent. GATE on encoder 4 steps normally and does not toggle.
12. Lower STEPS below HITS and ROTATE. Pass: the shown values clamp and the pattern stays valid.
13. Step the playhead through all 16 slots. Pass: exactly one tick shows, at the current step. The black covers must not be visible against the page background.
14. Check that the ring and centre text fit below the header and are legible.
15. Save and reload a project. Pass: all eight parameters return.
