#include <algorithm>
#include <cstring>
#include "harness.hpp"

typedef std::vector<u32> Seq;
using namespace machine;

enum { R2T, R4, R4T, R8, R8T, R16, R16T, R32, R32T };

// The sequence of one cycle, as next_index gives it (kChordHit as -1 for readability).
static Seq cycle(u32 pattern, u32 n) {
    Walker w = {};
    Seq out;
    const u32 length = cycle_length(pattern, n);
    for (u32 step = 0; step < length; ++step) out.push_back(next_index(pattern, step, n, w));
    return out;
}

static const u32 H = kChordHit;

// ---- Patterns, N = 1, 2, 3, 4 and 8 ----
static void patterns() {
    // N = 1: every pattern plays that one note, once per cycle.
    for (u32 p = Up; p < kPatterns; ++p) require(cycle_length(p, 1) == 1 && cycle(p, 1) == Seq({0}));

    struct Case { u32 pattern; Seq n2, n3, n4; };
    const Case cases[] = {
        {Up, {0, 1}, {0, 1, 2}, {0, 1, 2, 3}},
        {Down, {1, 0}, {2, 1, 0}, {3, 2, 1, 0}},
        {UpDown, {0, 1}, {0, 1, 2, 1}, {0, 1, 2, 3, 2, 1}},
        {DownUp, {1, 0}, {2, 1, 0, 1}, {3, 2, 1, 0, 1, 2}},
        {UpDownBoth, {0, 1, 1, 0}, {0, 1, 2, 2, 1, 0}, {0, 1, 2, 3, 3, 2, 1, 0}},
        {DownUpBoth, {1, 0, 0, 1}, {2, 1, 0, 0, 1, 2}, {3, 2, 1, 0, 0, 1, 2, 3}},
        {Order, {0, 1}, {0, 1, 2}, {0, 1, 2, 3}},
        {Converge, {0, 1}, {0, 2, 1}, {0, 3, 1, 2}},
        {Diverge, {1, 0}, {1, 2, 0}, {2, 1, 3, 0}},
        {Pinky, {0, 1}, {0, 2, 1, 2}, {0, 3, 1, 3, 2, 3}},
        {Thumb, {0, 1}, {0, 1, 0, 2}, {0, 1, 0, 2, 0, 3}},
        {Stair, {0, 1}, {0, 1, 2}, {0, 1, 2, 1, 2, 3}},
        {StairUpFalling, {0, 1}, {2, 1, 0}, {2, 1, 0, 3, 2, 1}},
        {StairDownFalling, {1, 0}, {2, 1, 0}, {3, 2, 1, 2, 1, 0}},
        {StairDownRising, {1, 0}, {0, 1, 2}, {1, 2, 3, 0, 1, 2}},
        {Chord, {H, H}, {H, H, H}, {H, H, H, H}},
        {ChordThenUp, {H, 0, 1}, {H, 0, 1, 2}, {H, 0, 1, 2, 3}},
    };
    for (const Case& c : cases) {
        require(cycle(c.pattern, 2) == c.n2 && cycle(c.pattern, 3) == c.n3 && cycle(c.pattern, 4) == c.n4);
        require(cycle_length(c.pattern, 2) == c.n2.size() && cycle_length(c.pattern, 3) == c.n3.size());
        require(cycle_length(c.pattern, 4) == c.n4.size());
    }
    // The spec's examples with C E G B: CONV C B E G, DIVRG G E B C, PINKY C B E B G B.
    // N = 8.
    require(cycle(Up, 8) == Seq({0, 1, 2, 3, 4, 5, 6, 7}) && cycle(Down, 8) == Seq({7, 6, 5, 4, 3, 2, 1, 0}));
    require(cycle(UpDown, 8) == Seq({0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1}));
    require(cycle(DownUp, 8) == Seq({7, 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6}));
    require(cycle(UpDownBoth, 8) == Seq({0, 1, 2, 3, 4, 5, 6, 7, 7, 6, 5, 4, 3, 2, 1, 0}));
    require(cycle(DownUpBoth, 8) == Seq({7, 6, 5, 4, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7}));
    require(cycle(Converge, 8) == Seq({0, 7, 1, 6, 2, 5, 3, 4}) && cycle(Diverge, 8) == Seq({4, 3, 5, 2, 6, 1, 7, 0}));
    require(cycle(Pinky, 8) == Seq({0, 7, 1, 7, 2, 7, 3, 7, 4, 7, 5, 7, 6, 7}));
    require(cycle(Thumb, 8) == Seq({0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7}));
    require(cycle(Stair, 8) == Seq({0, 1, 2, 1, 2, 3, 2, 3, 4, 3, 4, 5, 4, 5, 6, 5, 6, 7}));
    require(cycle(StairUpFalling, 8) == Seq({2, 1, 0, 3, 2, 1, 4, 3, 2, 5, 4, 3, 6, 5, 4, 7, 6, 5}));
    require(cycle(StairDownFalling, 8) == Seq({7, 6, 5, 6, 5, 4, 5, 4, 3, 4, 3, 2, 3, 2, 1, 2, 1, 0}));
    require(cycle(StairDownRising, 8) == Seq({5, 6, 7, 4, 5, 6, 3, 4, 5, 2, 3, 4, 1, 2, 3, 0, 1, 2}));
    // Every stair stays within the notes, for every size.
    for (u32 p : {u32(Stair), u32(StairUpFalling), u32(StairDownFalling), u32(StairDownRising)})
        for (u32 n = 1; n <= 32; ++n)
            for (u32 i : cycle(p, n)) require(i < n);
    require(cycle(ChordThenUp, 8) == Seq({H, 0, 1, 2, 3, 4, 5, 6, 7}) && cycle_length(Chord, 8) == 8);
    // DIVRG is CONV reversed, for every size.
    for (u32 n = 1; n <= 32; ++n) {
        Seq conv = cycle(Converge, n);
        std::reverse(conv.begin(), conv.end());
        require(cycle(Diverge, n) == conv);
    }

    // The random patterns, for N = 2, 3, 4 and 8.
    for (u32 n : {2u, 3u, 4u, 8u}) {
        Walker w = {};
        u32 previous_last = 99;
        for (u32 round = 0; round < 200; ++round) {
            Seq order;
            for (u32 step = 0; step < cycle_length(Shuffle, n); ++step) order.push_back(next_index(Shuffle, step, n, w));
            Seq sorted = order;
            std::sort(sorted.begin(), sorted.end());
            for (u32 i = 0; i < n; ++i) require(sorted[i] == i);  // each note once
            require(order[0] != previous_last);                    // never starts on the last note
            previous_last = order.back();
        }
        Walker r = {};
        std::vector<u32> seen(n, 0);
        for (u32 i = 0; i < 400 * n; ++i) ++seen[next_index(Random, i % n, n, r)];
        for (u32 count : seen) require(count > 200);  // every note, roughly evenly
        Walker d = {};
        u32 at = next_index(Drunk, 0, n, d);
        for (u32 i = 1; i < 500; ++i) {
            const u32 next = next_index(Drunk, i % n, n, d);  // the walk carries across cycles
            require(next < n && (next + 1 == at || at + 1 == next));
            at = next;
        }
        require(cycle_length(Shuffle, n) == n && cycle_length(Random, n) == n && cycle_length(Drunk, n) == n);
    }
}

// ---- The chain ----

struct Chain : Host {
    explicit Chain(u32 a, u32 b = Off, u32 c = Off, u32 d = Off, u32 cycles = 1, bool latch = false,
                   u32 octave = 1, u32 rate = R16) {
        set(0, a, kPatterns); set(1, b, kPatterns); set(2, c, kPatterns); set(3, d, kPatterns);
        set(4, rate, kRates); set(5, 8, kGates); set(6, octave - 1, 4);
        set(7, (cycles - 1) + (latch ? 8 : 0), kCycleChoices);
    }
    void hold(const Seq& keys) { for (u32 k : keys) press(k); }
};

static Seq notes_of(const Host& h) { return h.pitches(); }

static void chain() {
    const Seq ceg = {60, 64, 67};
    // CYCLES 2: slot 1 UP twice, slot 2 OFF skipped, slot 3 DOWN twice, slot 4 OFF, then slot 1 again.
    Chain h(Up, Off, Down, Off, 2);
    h.hold({67, 60, 64});
    h.step(60 * 15);
    require(notes_of(h) == Seq({60, 64, 67, 60, 64, 67, 67, 64, 60, 67, 64, 60, 60, 64, 67}));
    for (u32 i = 0; i < h.events.size(); ++i) if (h.events[i].on) require(h.events[i].time % 60 == 0);

    // All four OFF: notes pass straight through to the instrument, and release normally.
    Chain off(Off);
    off.press(60);
    require(off.passed_through == 1 && off.events.empty());
    off.release(60);
    require(off.state.active == 0);

    // A new phrase restarts at the first non-OFF slot, step 1.
    Chain phrase(Off, Up, Down, Off, 1);
    phrase.hold(ceg);
    phrase.step(60 * 4);  // UP, then DOWN's first note
    for (u32 k : ceg) phrase.release(k);
    phrase.events.clear();
    phrase.hold(ceg);
    phrase.step(60 * 3);
    require(notes_of(phrase) == Seq({60, 64, 67}));

    // Patterns follow OCTAVE; CHORD steps through the octaves, one per hit; CHD/1 then plays UP.
    Chain oct(Up, Chord, ChordThenUp, Off, 1, false, 2);
    oct.hold({60, 67});
    oct.step(60 * (4 + 4 + 5));
    // A chord's notes start highest first, lowest last.
    const Seq up = {60, 67, 72, 79}, chords = {67, 60, 79, 72, 67, 60, 79, 72}, chd = {67, 60, 60, 67, 72, 79};
    Seq all = up;
    all.insert(all.end(), chords.begin(), chords.end());
    all.insert(all.end(), chd.begin(), chd.end());
    require(notes_of(oct) == all);
    require(oct.largest_drain <= 8 && oct.stale_updates == 0);

    // Changing a playing slot's pattern applies at its next cycle.
    Chain change(Up, Off, Off, Off, 4);
    change.hold(ceg);
    change.step(60 * 2);
    change.set(0, Down, kPatterns);
    change.step(60 * 4);
    require(notes_of(change) == Seq({60, 64, 67, 67, 64, 60}));

    // Held notes changing mid-slot: the list rebuilds and the step continues.
    Chain grow(Up, Off, Off, Off, 2);
    grow.hold({60, 64});
    grow.step(60);
    grow.press(67);
    grow.step(60 * 3);
    require(notes_of(grow) == Seq({60, 64, 67, 60}));
    // Fewer notes mid-cycle: the step wraps into the shorter cycle (step 4 of 2 notes -> the second).
    Chain shrink(Up, Off, Off, Off, 4);
    shrink.hold({60, 62, 64, 65});
    shrink.step(60 * 2 + 10);  // 60, 62, 64 played; step 4 is next
    shrink.release(64);
    shrink.release(65);
    shrink.step(60 * 4);
    require(notes_of(shrink) == Seq({60, 62, 64, 62, 60, 62, 60}));
}

// Chords played by hand: a key arriving alone waits about 15 ms (7 player steps
// without a tempo) for the rest, then the lowest key starts; nothing is struck
// twice, and the steps still count from the first key. Keys arriving together
// (a sequenced chord) start at once. A cycle only ends at the step after its
// last, so keys landing later lengthen it instead of repeating the first note.
static void hand_played_chords() {
    // About 15 ms at any tempo: 7 steps at 120 BPM, 3 at 60, 18 at 300, and under half a step.
    require(chord_window(1200, 60) == 7 && chord_window(600, 60) == 3 && chord_window(3000, 60) == 18);
    require(chord_window(3000, 20) == 10 && chord_window(0, 60) == 7);
    struct Case { u32 gap; Seq keys, expected; u32 first; };
    const Case cases[] = {
        {0, {60, 64, 67}, {60, 64, 67, 60, 64, 67, 60}, 0},  // sequenced: at once
        {2, {60, 64, 67}, {60, 64, 67, 60, 64, 67, 60}, 7},  // by hand, 4 ms apart
        {2, {67, 60, 64}, {60, 64, 67, 60, 64, 67, 60}, 7},  // G first: still starts on C
        {30, {60, 64, 67}, {60, 64, 67, 60, 64, 67, 60}, 7}, // 60 ms apart: C alone, then the rest join
        {0, {64}, {64, 64, 64, 64, 64, 64, 64}, 7},          // a single key waits too
    };
    for (const Case& c : cases) {
        Chain h(Up);
        const u32 start = h.now;
        h.press(c.keys[0]);
        h.step(c.gap);
        for (size_t i = 1; i < c.keys.size(); ++i) h.press(c.keys[i]);
        h.step(60 * 6 + 1 - c.gap);
        std::vector<Event> on;
        for (const Event& e : h.events) if (e.on) on.push_back(e);
        require(notes_of(h) == c.expected);
        require(on[0].time == start + c.first);
        for (size_t i = 1; i < on.size(); ++i) require(on[i].time == start + 60 * i);  // the grid counts from the first key
        require(h.stale_updates == 0 && h.sounding() <= 1);
    }
}

// A monophonic instrument keeps the last note of a chord: every chord step, on
// every repeat of a sequenced chord, starts its lowest note last.
static void chord_order() {
    for (u32 pattern : {u32(Chord), u32(ChordThenUp)}) {
        Chain h(pattern, Off, Off, Off, 1, false, 2);
        for (u32 loop = 0; loop < 4; ++loop) {
            h.hold({71, 67, 64, 60});  // a sequenced chord arrives highest first
            h.step(60 * 16);
            for (u32 k : {71u, 67u, 64u, 60u}) h.release(k);
        }
        std::vector<Event> on;
        for (const Event& e : h.events) if (e.on) on.push_back(e);
        u32 chords = 0;
        for (size_t i = 0; i < on.size(); ++i) {
            const bool last = i + 1 == on.size() || on[i + 1].time != on[i].time;
            const bool alone = (i == 0 || on[i - 1].time != on[i].time) && last;
            if (!last || alone) continue;
            // The last note-on at this time is the chord's lowest.
            u32 lowest = 127;
            for (size_t j = i + 1; j-- > 0 && on[j].time == on[i].time;) lowest = std::min(lowest, on[j].pitch);
            require(on[i].pitch == lowest);
            ++chords;
        }
        require(chords >= (pattern == Chord ? 64u : 8u));
        require(h.stale_updates == 0 && h.largest_drain <= 8);
    }
    // Releasing the key that carries a chord stops it; the next step plays on.
    Chain c(Chord);
    c.hold({60, 64, 67});
    c.step(10);
    require(c.sounding() == 3);
    c.release(60);
    require(c.sounding() == 0);
    c.step(60);
    require(c.sounding() == 2);
}

static void latch() {
    // Latch keeps the notes after the keys are released.
    Chain h(Up, Off, Off, Off, 1, true);
    h.hold({60, 64, 67});
    h.step(60);
    for (u32 k : {60u, 64u, 67u}) h.release(k);
    h.step(60 * 6);
    require(notes_of(h) == Seq({60, 64, 67, 60, 64, 67, 60}));
    // Keys pressed after all were released replace the set and start a new phrase...
    h.events.clear();
    h.press(62);
    h.press(65);
    h.step(60 * 4);
    require(notes_of(h) == Seq({62, 65, 62, 65}));
    require(h.stale_updates == 0);
    for (u32 old : {60u, 64u, 67u}) require(!h.find(old));
    // ...while keys added during a hold join it.
    h.press(69);
    h.events.clear();
    h.step(60 * 3);
    Seq joined = notes_of(h);
    std::sort(joined.begin(), joined.end());
    require(joined == Seq({62, 65, 69}));
    for (u32 k : {62u, 65u, 69u}) h.release(k);
    // Turning latch off lets the latched notes go.
    h.set(7, 0, kCycleChoices);
    h.step(60 * 2);
    require(h.sounding() == 0 && h.state.active == 0);
    for (u32 k : {62u, 65u, 69u}) require(!h.find(k));
    h.step(300);
    require(h.events.back().on == false && h.sounding() == 0);

    // Eight latched keys: one is let go, so a new key always has room.
    Chain full(Up, Off, Off, Off, 1, true);
    for (u32 i = 0; i < 8; ++i) full.press(60 + i);
    for (u32 i = 0; i < 8; ++i) full.release(60 + i);
    require(count_bits(full.state.active) == 7);
    full.press(48);
    full.step(60 * 2);
    require(full.pitches().back() == 48 && full.stale_updates == 0 && full.largest_drain <= 8);
}

// Placeholders are queued far in the future, at velocity 1.
static void placeholders() {
    Chain h(Up, Off, Off, Off, 1);
    player::Plan plan;
    require(press(h.state, h.controls, 60, 100, plan) && plan.count == 1);
    require(plan.tones[0].velocity == 1 && plan.tones[0].delay == kParkedDelay && plan.tones[0].pitch == 60);
}

static void cleanup() {
    Chain h(Up, Down, Chord, Shuffle, 2);
    for (u32 i = 0; i < 9; ++i) h.press(60 + i);
    require(h.passed_through == 0 && h.held == 8 && !h.find(68));
    h.release(68);
    h.step(60 * 40);
    for (u32 i = 0; i < 8; ++i) { h.release(60 + i); h.step(17); }
    require(h.sounding() == 0 && h.stale_updates == 0 && h.largest_drain <= 8);
    h.hold({60, 64});
    h.step(30);
    h.all_notes_off();
    require(h.sounding() == 0);

    // Keys held while every slot is OFF sound nothing for a long time; their parked
    // placeholders are renewed and never sound.
    Chain idle(Up, Off, Off, Off, 1, false, 1, R4);
    idle.hold({60, 64, 67});
    idle.step(10);
    idle.set(0, Off, kPatterns);
    idle.step(140000);
    for (const Event& e : idle.events) if (e.on) require(e.velocity != 1);
    require(idle.stale_updates == 0 && idle.sounding() == 0);
}

static void screen() {
    Chain h(Up, Pinky, Off, ChordThenUp, 3, true, 2, R16T);
    player::Display idle = {};
    display(h.state, h.controls, idle);
    const char* const expected[8] = {"up", "pinky", "off", "chd/1", "1/16t", "50%", "2", "3L"};
    for (u32 i = 0; i < 8; ++i) {
        require(std::memchr(idle.values[i], 0, sizeof idle.values[i]) != nullptr);
        require(std::strcmp(idle.values[i], expected[i]) == 0);
    }
    require(std::strcmp(idle.text, "ready") == 0 && idle.lit == (0x7u << 4));

    Chain p(Up, Down, Off, Off, 2);
    p.hold({61, 64, 67});
    p.step(60 * 6 + 1);  // after UP's two cycles: slot 2 (DOWN), first cycle, first step
    player::Display on = {};
    display(p.state, p.controls, on);
    require(std::strcmp(on.text, "arp 2  G4") == 0);
    require((on.lit & 0xfu) == 0x2u);                      // slot 2's bar
    require(((on.lit >> 4) & 0xffu) == 0x1u);              // its first cycle of two
    require(((on.lit >> 12) & 0xfffu) == 0xfu);            // one step of three: 4 of 12 cells
    Chain all(Off);
    player::Display none = {};
    display(all.state, all.controls, none);
    require(std::strcmp(none.text, "all off") == 0);
}

int main() {
    static_assert(sizeof(State) <= player::state_bytes, "State fits the SDK's area");
    patterns();
    chain();
    hand_played_chords();
    chord_order();
    latch();
    placeholders();
    cleanup();
    screen();
    std::puts("chained_arp: all checks passed");
    return 0;
}
