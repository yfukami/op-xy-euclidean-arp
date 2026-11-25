// Euclidean Arp: an arpeggiator whose rhythm is a Euclidean pattern.
//
// A player has no note-on/note-off output and no clock callback, so:
// - each sounding note lives in the chord record of the key that produced it;
// - a held key that is silent keeps its record with a far-future queued tone,
//   because an empty Replace would release the root for good;
// - update() is drained before every player step and after every press and
//   release, so press and release mark their drain and the others are the clock.
#include <opxy/divide.hpp>
#include <opxy/player.hpp>

namespace euclid_arp {
using opxy::i32;
using opxy::u8;
using opxy::u16;
using opxy::u32;
using opxy::player::Controls;
using opxy::player::Display;
using opxy::player::Plan;
using opxy::player::Tone;
using opxy::player::Update;

const u32 kSlots = opxy::player::max_tones;  // the SDK holds at most eight roots
const u32 kOctaves = 4;
const u32 kPool = kSlots * kOctaves;
const u32 kMaxSteps = 16;
const u32 kDrainLimit = 8;             // the SDK's updates per drain
const u16 kParkedDelay = 65535;        // player steps before a parked tone would sound
const u32 kRearmInterval = 32768;      // parked tones are renewed well before that
const u32 kDefaultChordWindow = 7;     // player steps, about 15 ms at 120 BPM

const u32 kRates = 9;
// The patterns, shared with Chain Arp, Seed Seq and Twin Arp. STYLE offers every
// one but Off, in this order.
enum Pattern {
    Off, Up, Down, UpDown, DownUp, UpDownBoth, DownUpBoth, Order, Converge, Diverge,
    Pinky, Thumb, Stair, StairUpFalling, StairDownFalling, StairDownRising,
    Shuffle, Random, Drunk, Chord, ChordThenUp
};
const u32 kPatterns = 21;
const u32 kStyles = kPatterns - 1;
const u32 kChordHit = 0xff;  // next_index's answer for a step that plays every held note

// The random styles' memory: SHUF's order, DRUNK's place and the generator.
struct Walker {
    u8 perm[kPool];
    u32 perm_count, last, drunk, rng;
    u8 drunk_placed;
};

struct Slot {
    u8 root, velocity, next_pitch, next_velocity;
    u32 order;
};

struct State {
    Slot slots[kSlots];
    u8 pool_pitch[kPool], pool_slot[kPool];
    u8 chord_pitch[kSlots], chord_velocity[kSlots];  // a chord hit's notes, highest first
    Walker walker;
    u32 pool_count, pool_style, pool_octaves, order_counter;
    u32 step, tick, gate_left, rearm, rng, start_wait;
    u32 notes, played_length;  // the pattern's next position, and its cycle's length when it last played
    u8 active, sounding, off_mask, on_mask;
    u8 pool_dirty, first_hit, started, event_pending;
    u8 draining, drain_calls, has_note, shown_pitch;
    u8 waiting;  // a key arrived alone: the first hit waits for the rest of a chord
    u8 chord_slot, chord_count;  // the key whose record sounds a chord, plus one; its notes
};

struct Params {
    u32 rate, style, octaves, gate, steps, hits, rotate, chance;
    bool keep_first;
};

inline u32 smaller(u32 a, u32 b) { return a < b ? a : b; }
// quotient16's divide steps fail when both operands share a register, which the
// compiler chooses when it can prove them equal (100 / 100 gave the "e" in "e00%").
inline u32 divide(u32 n, u32 d) { return n == d ? 1 : opxy::divide::quotient16(n, d); }
inline u32 modulo(u32 n, u32 d) { return n - d * divide(n, d); }

inline Params read_params(const Controls& c) {
    using opxy::player::choice;
    // Nine choices, so that projects saved with 1/4, 1/8, 1/16 and 1/32 keep them.
    static const u16 rates[kRates] = {320, 240, 160, 120, 80, 60, 40, 30, 20};
    Params p;
    p.rate = rates[choice(c.value[0], kRates)];
    p.style = 1 + choice(c.value[1], kStyles);
    p.octaves = 1 + choice(c.value[2], kOctaves);
    p.gate = 10 + 5 * choice(c.value[3], 19);
    p.steps = 1 + choice(c.value[4], kMaxSteps);
    p.hits = smaller(choice(c.value[5], kMaxSteps + 1), p.steps);
    p.rotate = smaller(choice(c.value[6], kMaxSteps), p.steps - 1);
    // 0..100%, then on past 100% down through 95*..0*, which keep each loop's first hit.
    const u32 amount = choice(c.value[7], 41);
    p.keep_first = amount > 20;
    p.chance = 5 * (p.keep_first ? 40 - amount : amount);
    return p;
}

// Maximally even, equal to Bjorklund's pattern up to rotation; constant time.
inline bool is_hit(u32 step, u32 steps, u32 hits, u32 rotate) {
    if (!hits) return false;
    return modulo(modulo(step + steps - rotate, steps) * hits, steps) < hits;
}

inline u32 find_slot(const State& s, u32 note) {
    for (u32 i = 0; i < kSlots; ++i)
        if ((s.active >> i & 1) && s.slots[i].root == note) return i;
    return kSlots;
}

inline void add_to_pool(State& s, u32 pitch, u32 slot) {
    for (u32 i = 0; i < s.pool_count; ++i)
        if (s.pool_pitch[i] == pitch) return;
    s.pool_pitch[s.pool_count] = static_cast<u8>(pitch);
    s.pool_slot[s.pool_count] = static_cast<u8>(slot);
    ++s.pool_count;
}

// Runs only when the held keys, STYLE or OCTAVE changed; bounded by kPool.
inline void rebuild_pool(State& s, const Params& p) {
    u8 ordered[kSlots];
    u32 count = 0;
    for (u32 i = 0; i < kSlots; ++i) {
        if (!(s.active >> i & 1)) continue;
        u32 at = count++;
        while (at && s.slots[ordered[at - 1]].order > s.slots[i].order) {
            ordered[at] = ordered[at - 1];
            --at;
        }
        ordered[at] = static_cast<u8>(i);
    }
    s.pool_count = 0;
    for (u32 octave = 0; octave < p.octaves; ++octave)
        for (u32 i = 0; i < count; ++i) {
            const u32 pitch = s.slots[ordered[i]].root + 12 * octave;
            if (pitch <= 127) add_to_pool(s, pitch, ordered[i]);
        }
    if (p.style != Order)
        for (u32 i = 1; i < s.pool_count; ++i) {
            const u8 pitch = s.pool_pitch[i], slot = s.pool_slot[i];
            u32 at = i;
            for (; at && s.pool_pitch[at - 1] > pitch; --at) {
                s.pool_pitch[at] = s.pool_pitch[at - 1];
                s.pool_slot[at] = s.pool_slot[at - 1];
            }
            s.pool_pitch[at] = pitch;
            s.pool_slot[at] = slot;
        }
    s.pool_style = p.style;
    s.pool_octaves = p.octaves;
    s.pool_dirty = 0;
}

inline u32 next_random(State& s) {
    u32 x = s.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s.rng = x;
    return x;
}

// ---- Patterns, as in Chain Arp: pure functions of the position and N, the note count ----

inline u32 cycle_length(u32 pattern, u32 n) {
    if (!n) return 0;
    if (n == 1) return 1;
    switch (pattern) {
    case UpDown:
    case DownUp: return 2 * n - 2;
    case UpDownBoth:
    case DownUpBoth: return 2 * n;
    case Pinky:
    case Thumb: return 2 * (n - 1);
    case Stair:
    case StairUpFalling:
    case StairDownFalling:
    case StairDownRising: return n < 3 ? n : 3 * (n - 2);
    case ChordThenUp: return 1 + n;
    default: return n;
    }
}

inline u32 walker_random(u32& rng) {
    u32 x = rng ? rng : 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng = x;
    return x;
}

inline u32 random_below(u32& rng, u32 n) { return ((walker_random(rng) >> 16) * n) >> 16; }

inline u32 converge(u32 step, u32 n) { return (step & 1) ? n - 1 - (step >> 1) : (step >> 1); }

// SHUF: every note once per cycle, never starting on the note that ended the last one.
inline void shuffle(Walker& w, u32 n) {
    for (u32 i = 0; i < n; ++i) w.perm[i] = static_cast<u8>(i);
    for (u32 i = n - 1; i > 0; --i) {
        const u32 j = random_below(w.rng, i + 1);
        const u8 swap = w.perm[i];
        w.perm[i] = w.perm[j];
        w.perm[j] = swap;
    }
    if (n > 1 && w.perm[0] == w.last) {
        const u8 swap = w.perm[0];
        w.perm[0] = w.perm[1];
        w.perm[1] = swap;
    }
    w.perm_count = n;
}

// The 0-based index into the note list at `step` of a cycle, or kChordHit.
inline u32 next_index(u32 pattern, u32 step, u32 n, Walker& w) {
    if (n <= 1) return 0;
    switch (pattern) {
    case Up:
    case Order: return step;
    case Down: return n - 1 - step;
    case UpDown: return step < n ? step : 2 * n - 2 - step;
    case DownUp: return n - 1 - (step < n ? step : 2 * n - 2 - step);
    case UpDownBoth: return step < n ? step : 2 * n - 1 - step;
    case DownUpBoth: return step < n ? n - 1 - step : step - n;
    case Converge: return converge(step, n);
    case Diverge: return converge(n - 1 - step, n);
    case Pinky: return (step & 1) ? n - 1 : step >> 1;
    case Thumb: return (step & 1) ? (step + 1) >> 1 : 0;
    // The stairs: groups of three, the staircase climbing up or down one note per
    // group, the notes in each group rising or falling. Under 3 notes: UP or DOWN.
    case Stair: return n < 3 ? step : divide(step, 3) + modulo(step, 3);
    case StairUpFalling: return n < 3 ? step : divide(step, 3) + 2 - modulo(step, 3);
    case StairDownFalling: return n < 3 ? n - 1 - step : n - 1 - divide(step, 3) - modulo(step, 3);
    case StairDownRising: return n < 3 ? n - 1 - step : n - 3 - divide(step, 3) + modulo(step, 3);
    case Shuffle: {
        if (!step || w.perm_count != n) shuffle(w, n);
        w.last = w.perm[step];
        return w.last;
    }
    case Random: return random_below(w.rng, n);
    case Drunk: {
        if (!w.drunk_placed || w.drunk >= n) {
            w.drunk = random_below(w.rng, n);
            w.drunk_placed = 1;
        } else if (walker_random(w.rng) & 0x10000u) {
            w.drunk = w.drunk + 1 < n ? w.drunk + 1 : w.drunk - 1;
        } else {
            w.drunk = w.drunk ? w.drunk - 1 : 1;
        }
        return w.drunk;
    }
    case Chord: return kChordHit;
    case ChordThenUp: return step ? step - 1 : kChordHit;
    default: return 0;
    }
}

inline void queue_note(State& s, u32 slot, u32 pitch, u32 velocity) {
    const u8 bit = static_cast<u8>(1u << slot);
    s.slots[slot].next_pitch = static_cast<u8>(pitch);
    s.slots[slot].next_velocity = static_cast<u8>(velocity);
    s.on_mask |= bit;
    s.off_mask &= static_cast<u8>(~bit);  // its Replace already ends the old note
}

inline bool roll_chance(State& s, u32 chance) {
    if (chance >= 100) return true;
    if (!chance) return false;
    return (((next_random(s) >> 16) * 100) >> 16) < chance;
}

// A skipped hit still moves the arpeggio on, so the melody keeps its place.
inline void ensure_pool(State& s, const Params& p) {
    if (s.pool_dirty || s.pool_style != p.style || s.pool_octaves != p.octaves) rebuild_pool(s, p);
}

inline void start_gate(State& s, const Params& p) {
    const u32 gate = divide(p.rate * p.gate, 100);
    s.gate_left = gate ? gate : 1;
}

// Plays (or, by CHANCE, skips) a hit. Each hit takes the pattern's next note; a
// skipped hit still moves the pattern on, so the melody keeps its place. If the
// held keys change, the position carries on into the new note list. Returns
// whether the hit sounded.
inline bool trigger(State& s, const Params& p) {
    s.off_mask |= s.sounding;
    s.chord_slot = 0;
    ensure_pool(s, p);
    const bool play = (p.keep_first && s.first_hit) || roll_chance(s, p.chance);
    s.first_hit = 0;
    const u32 n = s.pool_count;
    if (!n) return false;
    // As in Chain Arp: a cycle that played to its end starts again, and a position
    // beyond a shorter note list wraps into it.
    const u32 length = cycle_length(p.style, n);
    if (s.notes >= length) s.notes = s.notes == s.played_length ? 0 : modulo(s.notes, length);
    const u32 at = s.notes++;
    s.played_length = length;
    const u32 index = next_index(p.style, at, n, s.walker);
    if (!play) return false;
    if (index == kChordHit) {
        // Every held key, CHORD climbing OCTAVE one octave per hit, from one record,
        // highest note first and lowest last, so a monophonic instrument keeps the
        // lowest note.
        const u32 shift = 12 * (p.style == Chord ? modulo(at, p.octaves) : 0);
        u32 lowest = kSlots;
        s.chord_count = 0;
        for (u32 i = 0; i < kSlots; ++i) {
            const u32 pitch = s.slots[i].root + shift;
            if (!(s.active >> i & 1) || pitch > 127) continue;
            u32 j = s.chord_count++;
            for (; j && s.chord_pitch[j - 1] < pitch; --j) {
                s.chord_pitch[j] = s.chord_pitch[j - 1];
                s.chord_velocity[j] = s.chord_velocity[j - 1];
            }
            s.chord_pitch[j] = static_cast<u8>(pitch);
            s.chord_velocity[j] = s.slots[i].velocity;
            if (lowest == kSlots || s.slots[i].root < s.slots[lowest].root) lowest = i;
        }
        if (lowest == kSlots) return false;
        s.chord_slot = static_cast<u8>(lowest + 1);
        queue_note(s, lowest, s.chord_pitch[s.chord_count - 1], s.slots[lowest].velocity);
        s.shown_pitch = s.chord_pitch[s.chord_count - 1];  // the lowest note
    } else {
        const u32 slot = s.pool_slot[index];
        queue_note(s, slot, s.pool_pitch[index], s.slots[slot].velocity);
        s.shown_pitch = s.pool_pitch[index];
    }
    s.has_note = 1;
    start_gate(s, p);
    return true;
}

inline u32 count_bits(u32 mask) {
    u32 count = 0;
    for (; mask; mask &= mask - 1) ++count;
    return count;
}

// How long a key that arrives alone waits for the rest of a hand-played chord:
// about 15 ms (a player step lasts 2,500,000 / bpm_x10 microseconds), always
// under half a RATE step, so it cannot reach the pattern's next step.
inline u32 chord_window(u32 bpm_x10, u32 rate) {
    u32 window = bpm_x10 ? divide(bpm_x10 * 3, 500) : kDefaultChordWindow;
    if (window < 1) window = 1;
    return smaller(window, rate / 2);
}

inline void advance_clock(State& s, const Params& p) {
    if (s.gate_left && --s.gate_left == 0) s.off_mask |= s.sounding;
    if (++s.rearm >= kRearmInterval) {
        s.rearm = 0;
        s.off_mask |= static_cast<u8>(s.active & ~s.sounding);
    }
    if (++s.tick < p.rate) return;
    s.tick = 0;
    if (++s.step >= p.steps) {
        s.step = 0;
        s.first_hit = 1;
    }
    if (is_hit(s.step, p.steps, p.hits, p.rotate)) trigger(s, p);
}

inline void clock_step(State& s, const Controls& c) {
    if (!s.active) return;
    const Params p = read_params(c);
    if (!s.started) {
        s.started = 1;
        // Keys that arrived together (a sequenced chord) play at once. A key that
        // arrived alone waits about 15 ms for the rest of a hand-played chord, so
        // the first note is the right one and nothing is struck twice (choosing it
        // again when a lower key landed sounded on the device as a two-note stab).
        // The steps still count from the first key.
        if (count_bits(s.active) >= 2) {
            if (is_hit(s.step, p.steps, p.hits, p.rotate)) trigger(s, p);
        } else {
            s.waiting = 1;
            s.start_wait = chord_window(c.bpm_x10, p.rate);
        }
        return;
    }
    if (s.waiting) {
        ++s.tick;
        if (--s.start_wait) return;
        s.waiting = 0;
        if (is_hit(s.step, p.steps, p.hits, p.rotate)) trigger(s, p);
        return;
    }
    advance_clock(s, p);
}

inline u32 lowest_bit(u32 mask) {
    u32 i = 0;
    while (!(mask >> i & 1)) ++i;
    return i;
}

inline void park(Tone& tone, u32 root) {
    tone.pitch = static_cast<u8>(root);
    tone.velocity = 1;
    tone.delay = kParkedDelay;
}

bool press(State& s, const Controls&, u32 note, u32 velocity, Plan& plan) {
    plan.count = 0;
    if (note > 127) return false;
    s.event_pending = 1;
    const bool first = !s.active;
    u32 slot = find_slot(s, note);
    if (slot == kSlots) {
        if (s.active == 0xff) return true;  // the SDK cannot hold a ninth root
        slot = lowest_bit(static_cast<u8>(~s.active));
    }
    const u8 bit = static_cast<u8>(1u << slot);
    Slot& key = s.slots[slot];
    key.root = static_cast<u8>(note);
    key.velocity = static_cast<u8>(velocity < 1 ? 1 : velocity > 127 ? 127 : velocity);
    key.order = ++s.order_counter;
    s.active |= bit;
    s.sounding &= static_cast<u8>(~bit);  // the plan replaces anything this root held
    if (s.chord_slot == slot + 1) s.chord_slot = 0;
    s.off_mask &= static_cast<u8>(~bit);
    s.on_mask &= static_cast<u8>(~bit);
    s.pool_dirty = 1;
    plan.count = 1;
    park(plan.tones[0], note);
    if (!first) return true;

    s.step = s.tick = s.gate_left = s.rearm = s.notes = s.played_length = s.start_wait = 0;
    s.started = s.has_note = s.waiting = s.chord_slot = 0;
    s.first_hit = 1;
    if (!s.rng) s.rng = 0x9e3779b9u ^ (note << 8) ^ key.velocity;
    s.walker.rng = next_random(s);  // the random styles start somewhere new each phrase
    s.walker.perm_count = 0;
    s.walker.last = 0xff;
    s.walker.drunk_placed = 0;
    return true;
}

bool release(State& s, u32 note) {
    s.event_pending = 1;
    const u32 slot = find_slot(s, note);
    if (slot == kSlots) return true;  // a key press() could not hold
    const u8 keep = static_cast<u8>(~(1u << slot));
    s.active &= keep;
    s.sounding &= keep;
    s.off_mask &= keep;
    s.on_mask &= keep;
    if (s.chord_slot == slot + 1) s.chord_slot = 0;
    s.pool_dirty = 1;
    if (!s.active) s.gate_left = 0;
    return false;  // stock releases this root's notes
}

bool update(State& s, const Controls& c, Update& change) {
    if (!s.draining) {
        s.draining = 1;
        s.drain_calls = 0;
        if (s.event_pending) s.event_pending = 0;
        else clock_step(s, c);
    }
    if (!s.off_mask && !s.on_mask) {
        s.draining = 0;
        return false;
    }
    change.mode = opxy::player::Replace;
    change.plan.count = 1;
    if (s.off_mask) {
        const u32 slot = lowest_bit(s.off_mask);
        const u8 bit = static_cast<u8>(1u << slot);
        s.off_mask &= static_cast<u8>(~bit);
        s.sounding &= static_cast<u8>(~bit);
        change.root = s.slots[slot].root;
        park(change.plan.tones[0], change.root);
    } else {
        const u32 slot = lowest_bit(s.on_mask);
        const u8 bit = static_cast<u8>(1u << slot);
        s.on_mask &= static_cast<u8>(~bit);
        s.sounding |= bit;
        change.root = s.slots[slot].root;
        change.plan.tones[0].pitch = s.slots[slot].next_pitch;
        change.plan.tones[0].velocity = s.slots[slot].next_velocity;
        change.plan.tones[0].delay = 0;
        if (s.chord_slot == slot + 1 && s.chord_count) {
            change.plan.count = s.chord_count;  // stock sounds a record's notes in this order
            for (u32 i = 0; i < s.chord_count; ++i) {
                change.plan.tones[i].pitch = s.chord_pitch[i];
                change.plan.tones[i].velocity = s.chord_velocity[i];
                change.plan.tones[i].delay = 0;
            }
        }
    }
    if (++s.drain_calls >= kDrainLimit) s.draining = 0;  // the SDK stops calling here
    return true;
}

// Stock reset and all-notes-off: stock has already silenced every record.
void reset(State& s) { __builtin_memset(&s, 0, sizeof s); }

inline void append(char* out, u32 size, u32& at, const char* text) {
    while (*text && at + 1 < size) out[at++] = *text++;
    out[at] = 0;
}

inline void append_number(char* out, u32 size, u32& at, u32 value) {
    char digits[4] = {0, 0, 0, 0};
    u32 count = 0;
    if (value >= 100) digits[count++] = static_cast<char>('0' + divide(value, 100));
    if (value >= 10) digits[count++] = static_cast<char>('0' + modulo(divide(value, 10), 10));
    digits[count] = static_cast<char>('0' + modulo(value, 10));
    append(out, size, at, digits);
}

inline void show_number(char (&out)[8], u32 value, const char* suffix) {
    u32 at = 0;
    append_number(out, sizeof out, at, value);
    append(out, sizeof out, at, suffix);
}

void display(const State& s, const Controls& c, Display& d) {
    static const char rates[kRates][6] = {"1/2t", "1/4", "1/4t", "1/8", "1/8t", "1/16", "1/16t", "1/32", "1/32t"};
    // Lower case: a header value has under 60 px, and "CHORD" or "DOWN" would reach the next title.
    static const char styles[kPatterns][6] = {"off", "up", "down", "up/dn", "dn/up", "u/d+", "d/u+", "order", "conv",
                                              "divrg", "pinky", "thmb", "stu+", "stu-", "std-", "std+", "shuf", "rand",
                                              "drunk", "chord", "chd/1"};
    static const char notes[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const Params p = read_params(c);
    u32 at = 0;
    append(d.values[0], sizeof d.values[0], at, rates[opxy::player::choice(c.value[0], kRates)]);
    at = 0;
    append(d.values[1], sizeof d.values[1], at, styles[p.style]);
    show_number(d.values[2], p.octaves, "");
    show_number(d.values[3], p.gate, "%");
    show_number(d.values[4], p.steps, "");
    show_number(d.values[5], p.hits, "");
    show_number(d.values[6], p.rotate, "");
    show_number(d.values[7], p.chance, p.keep_first ? "%*" : "%");

    // Dots 0..15 are hits. Dots 16..19 each draw a tick in all four quadrants,
    // and dots 20..23 cover a quadrant's ticks, leaving the one playhead tick.
    for (u32 i = 0; i < p.steps; ++i)
        if (is_hit(i, p.steps, p.hits, p.rotate)) d.lit |= 1u << i;
    at = 0;
    if (s.active) {
        const u32 head = s.step & (kMaxSteps - 1);
        d.lit |= 1u << (16 + (head & 3));
        for (u32 quadrant = 0; quadrant < 4; ++quadrant)
            if (quadrant != head >> 2) d.lit |= 1u << (20 + quadrant);
    }
    if (s.active && s.has_note) {
        const u32 octave = divide(s.shown_pitch, 12);
        append(d.text, sizeof d.text, at, notes[s.shown_pitch - 12 * octave]);
        if (octave) append_number(d.text, sizeof d.text, at, octave - 1);
        else append(d.text, sizeof d.text, at, "-1");
    } else {
        append_number(d.text, sizeof d.text, at, p.hits);
        append(d.text, sizeof d.text, at, "/");
        append_number(d.text, sizeof d.text, at, p.steps);
    }
}
}  // namespace euclid_arp
