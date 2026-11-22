// Chain Arp: four arpeggiator slots played one after another. Each slot plays
// its pattern for CYCLES complete cycles, then hands over to the next slot that
// is not OFF: 1 -> 2 -> 3 -> 4 -> 1. CYCLES 1L..8L also latch the held notes.
//
// As in Euclidean Arp (device-tested): each sounding note lives in the chord
// record of a held key; a key that sounds nothing keeps a far-future parked
// tone, because only an empty Replace (used deliberately below, to let latched
// keys go) releases a root, for good; and update() is drained before every
// player step and after every press and release, so press and release mark
// their drain and the others are the clock.
//
// Latch: release() returning true skips stock's release, so a released key's
// record (and its note) stays; an empty Replace lets it go later.
#include <opxy/divide.hpp>
#include <opxy/player.hpp>

namespace chained_arp {
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
const u32 kChain = 4;                  // pattern slots in the chain
const u32 kDrainLimit = 8;             // the SDK's updates per drain
const u16 kParkedDelay = 65535;        // player steps before a parked tone would sound
const u32 kRearmInterval = 32768;      // parked tones are renewed well before that
const u32 kRates = 9;
const u32 kGates = 19;                 // 10..100% in steps of 5
const u32 kCycleChoices = 16;          // 1..8, then 1L..8L
const u32 kProgressDots = 12;
const u32 kDefaultChordWindow = 7;     // player steps, about 15 ms at 120 BPM

enum Pattern {
    Off, Up, Down, UpDown, DownUp, UpDownBoth, DownUpBoth, Order, Converge, Diverge,
    Pinky, Thumb, Stair, StairUpFalling, StairDownFalling, StairDownRising,
    Shuffle, Random, Drunk, Chord, ChordThenUp
};
const u32 kPatterns = 21;
const u32 kChordHit = 0xff;  // next_index's answer for a step that plays every held note

struct Key {
    u8 root, velocity;
    u32 order;
};

// The random patterns' memory: SHUF's order, DRUNK's place and the generator.
struct Walker {
    u8 perm[kPool];
    u32 perm_count, last, drunk, rng;
    u8 drunk_placed;
};

struct Params {
    u32 pattern[kChain], rate, gate, octaves, cycles;
    bool latch;
};

struct State {
    Key keys[kSlots];
    u8 sorted_pitch[kPool], sorted_slot[kPool];    // low to high, across OCTAVE
    u8 ordered_pitch[kPool], ordered_slot[kPool];  // in press order, octave by octave
    u8 key_pitch[kSlots];                          // the pitch each key's record sounds
    u8 chord_pitch[kSlots], chord_velocity[kSlots]; // a chord step's notes, highest first
    u32 chord_count;
    Walker walker;
    u32 pass_through[4];                           // notes press() let through to the instrument
    u32 pool_count, pool_octaves, order_counter, tick, gate_left, rearm, start_wait;
    u32 step, cycle_length, cycles_done;           // the next step, and the cycle's length when it last played
    u32 shown_step, shown_length, shown_cycle;     // where it played last, for the screen
    u8 held, active, sounding, dirty, releasing;   // key masks
    u8 pool_dirty, event_pending, draining, drain_calls, started, latch_on, waiting;
    u8 chain_slot, cycle_pattern, shown_slot, has_note, shown_pitch, shown_chord;
    u8 chord_carrier;  // the key whose record sounds the chord, plus one; 0 when none
};

inline u32 smaller(u32 a, u32 b) { return a < b ? a : b; }
// quotient16's divide steps fail when both operands share a register, which the
// compiler chooses when it can prove them equal (100 / 100 gave the "e" in "e00%").
inline u32 divide(u32 n, u32 d) { return n == d ? 1 : opxy::divide::quotient16(n, d); }
inline u32 modulo(u32 n, u32 d) { return n - d * divide(n, d); }

inline Params read_params(const Controls& c) {
    using opxy::player::choice;
    static const u16 rates[kRates] = {320, 240, 160, 120, 80, 60, 40, 30, 20};
    Params p;
    for (u32 i = 0; i < kChain; ++i) p.pattern[i] = choice(c.value[i], kPatterns);
    p.rate = rates[choice(c.value[4], kRates)];
    p.gate = 10 + 5 * choice(c.value[5], kGates);
    p.octaves = 1 + choice(c.value[6], kOctaves);
    const u32 cycles = choice(c.value[7], kCycleChoices);
    p.latch = cycles >= 8;
    p.cycles = 1 + (cycles & 7);
    return p;
}

inline bool all_off(const Params& p) {
    for (u32 i = 0; i < kChain; ++i)
        if (p.pattern[i] != Off) return false;
    return true;
}

// The first slot from `from` on, in chain order, that is not OFF; kChain if none.
inline u32 next_slot(const Params& p, u32 from) {
    for (u32 i = 0; i < kChain; ++i) {
        const u32 slot = (from + i) & (kChain - 1);
        if (p.pattern[slot] != Off) return slot;
    }
    return kChain;
}

// ---- Patterns: pure functions of the step and N, the expanded note count ----

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

inline u32 next_random(u32& rng) {
    u32 x = rng ? rng : 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng = x;
    return x;
}

inline u32 random_below(u32& rng, u32 n) { return ((next_random(rng) >> 16) * n) >> 16; }

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
        } else if (next_random(w.rng) & 0x10000u) {
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

// ---- Keys and the note lists ----

inline u32 count_bits(u32 mask) {
    u32 count = 0;
    for (; mask; mask &= mask - 1) ++count;
    return count;
}

inline u32 find_slot(const State& s, u32 note) {
    for (u32 i = 0; i < kSlots; ++i)
        if (((s.active | s.releasing) >> i & 1) && s.keys[i].root == note) return i;
    return kSlots;
}

inline void insert(u8* pitches, u8* slots, u32& count, u32 pitch, u32 slot, bool sorted) {
    for (u32 i = 0; i < count; ++i)
        if (pitches[i] == pitch) return;
    u32 at = count++;
    if (sorted)
        for (; at && pitches[at - 1] > pitch; --at) {
            pitches[at] = pitches[at - 1];
            slots[at] = slots[at - 1];
        }
    pitches[at] = static_cast<u8>(pitch);
    slots[at] = static_cast<u8>(slot);
}

// Both note lists, across OCTAVE; bounded by kPool.
inline void rebuild_pool(State& s, u32 octaves) {
    u8 ordered[kSlots];
    u32 keys = 0;
    for (u32 i = 0; i < kSlots; ++i) {
        if (!(s.active >> i & 1)) continue;
        u32 at = keys++;
        for (; at && s.keys[ordered[at - 1]].order > s.keys[i].order; --at) ordered[at] = ordered[at - 1];
        ordered[at] = static_cast<u8>(i);
    }
    u32 sorted_count = 0, ordered_count = 0;
    for (u32 octave = 0; octave < octaves; ++octave)
        for (u32 i = 0; i < keys; ++i) {
            const u32 pitch = s.keys[ordered[i]].root + 12 * octave;
            if (pitch > 127) continue;
            insert(s.sorted_pitch, s.sorted_slot, sorted_count, pitch, ordered[i], true);
            insert(s.ordered_pitch, s.ordered_slot, ordered_count, pitch, ordered[i], false);
        }
    s.pool_count = sorted_count;
    s.pool_octaves = octaves;
    s.pool_dirty = 0;
}

// ---- Playing ----

inline void mark(State& s, u8 keys) { s.dirty |= keys; }

inline void silence(State& s) {
    mark(s, s.sounding);
    s.sounding = 0;
    s.gate_left = 0;
    s.chord_carrier = 0;
    s.chord_count = 0;
}

// A key's record is replaced or released: whatever it sounded has stopped.
inline void forget(State& s, u8 keys) {
    s.sounding &= static_cast<u8>(~keys);
    s.dirty &= static_cast<u8>(~keys);
    if (s.chord_carrier && (keys >> (s.chord_carrier - 1) & 1)) {
        s.chord_carrier = 0;
        s.chord_count = 0;
    }
}

inline void sound(State& s, u32 key, u32 pitch) {
    s.key_pitch[key] = static_cast<u8>(pitch);
    s.sounding |= static_cast<u8>(1u << key);
    mark(s, static_cast<u8>(1u << key));
}

// Plays the chain's next step. A cycle ends here, at the step after its last,
// so that keys arriving meanwhile (a chord played by hand) lengthen the cycle
// instead of ending it early; if the notes changed so that the step lies past a
// shorter cycle, the step wraps into it instead.
inline void play_step(State& s, const Params& p) {
    silence(s);
    if (s.pool_dirty || s.pool_octaves != p.octaves) rebuild_pool(s, p.octaves);
    const u32 n = s.pool_count;
    if (!n) return;
    if (s.step) {
        const u32 length = cycle_length(s.cycle_pattern, n);
        if (s.step >= length) {
            if (s.step == s.cycle_length) {  // the cycle played to its end
                s.step = 0;
                if (++s.cycles_done >= p.cycles) {
                    s.cycles_done = 0;
                    s.chain_slot = static_cast<u8>(next_slot(p, (s.chain_slot + 1) & (kChain - 1)));
                    if (s.chain_slot >= kChain) s.chain_slot = 0;
                }
            } else {
                s.step = modulo(s.step, length);  // the held notes changed mid-slot
            }
        }
    }
    if (!s.step) {  // a cycle's start: OFF slots are skipped, and a changed pattern applies
        if (p.pattern[s.chain_slot] == Off) {
            s.chain_slot = static_cast<u8>(next_slot(p, s.chain_slot));
            s.cycles_done = 0;
        }
        if (s.chain_slot >= kChain) {  // every slot is OFF
            s.chain_slot = 0;
            return;
        }
        s.cycle_pattern = static_cast<u8>(p.pattern[s.chain_slot]);
    }
    const u32 length = cycle_length(s.cycle_pattern, n);
    const u32 index = next_index(s.cycle_pattern, s.step, n, s.walker);
    s.shown_slot = s.chain_slot;
    s.shown_step = s.step;
    s.shown_length = length;
    s.shown_cycle = s.cycles_done;
    if (index == kChordHit) {
        // CHORD steps through the octaves, one per hit; CHD/1's chord is the held notes.
        // The whole chord sounds from one record, highest note first and lowest last,
        // so a monophonic instrument always keeps the lowest note.
        const u32 octave = s.cycle_pattern == Chord ? modulo(s.step, p.octaves) : 0;
        u32 lowest_key = kSlots;
        s.chord_count = 0;
        for (u32 i = 0; i < kSlots; ++i) {
            const u32 pitch = s.keys[i].root + 12 * octave;
            if (!(s.active >> i & 1) || pitch > 127) continue;
            u32 at = s.chord_count++;
            for (; at && s.chord_pitch[at - 1] < pitch; --at) {
                s.chord_pitch[at] = s.chord_pitch[at - 1];
                s.chord_velocity[at] = s.chord_velocity[at - 1];
            }
            s.chord_pitch[at] = static_cast<u8>(pitch);
            s.chord_velocity[at] = s.keys[i].velocity;
            if (lowest_key == kSlots || s.keys[i].root < s.keys[lowest_key].root) lowest_key = i;
        }
        if (lowest_key < kSlots) {
            s.chord_carrier = static_cast<u8>(lowest_key + 1);
            s.sounding |= static_cast<u8>(1u << lowest_key);
            mark(s, static_cast<u8>(1u << lowest_key));
            s.shown_pitch = s.chord_pitch[s.chord_count - 1];
        }
        s.shown_chord = 1;
    } else {
        const bool ordered = s.cycle_pattern == Order;
        const u32 slot = ordered ? s.ordered_slot[index] : s.sorted_slot[index];
        const u32 pitch = ordered ? s.ordered_pitch[index] : s.sorted_pitch[index];
        sound(s, slot, pitch);
        s.shown_pitch = static_cast<u8>(pitch);
        s.shown_chord = 0;
    }
    s.has_note = 1;
    const u32 gate = divide(p.rate * p.gate, 100);
    s.gate_left = gate ? gate : 1;
    s.cycle_length = length;
    ++s.step;
}

// How long a key that arrives alone waits for the rest of a hand-played chord:
// about 15 ms (a player step lasts 2,500,000 / bpm_x10 microseconds), always
// under half a step.
inline u32 chord_window(u32 bpm_x10, u32 rate) {
    u32 window = bpm_x10 ? divide(bpm_x10 * 3, 500) : kDefaultChordWindow;
    if (window < 1) window = 1;
    return smaller(window, rate / 2);
}

inline void start_phrase(State& s, const Params& p) {
    const u32 first = next_slot(p, 0);
    s.chain_slot = static_cast<u8>(first < kChain ? first : 0);
    s.step = s.cycles_done = s.cycle_length = s.tick = s.gate_left = s.rearm = s.start_wait = 0;
    s.started = s.has_note = s.waiting = 0;
}

// Keys let go of with latch off. Their records end with an empty Replace.
inline void let_go(State& s, u8 keys) {
    if (!keys) return;
    s.releasing |= keys;
    s.active &= static_cast<u8>(~keys);
    forget(s, keys);
    s.pool_dirty = 1;
}

inline void clock_step(State& s, const Controls& c) {
    const Params p = read_params(c);
    s.latch_on = p.latch;
    if (!p.latch) let_go(s, static_cast<u8>(s.active & ~s.held));  // turning latch off
    if (!s.active) {
        s.started = 0;
        return;
    }
    if (!s.started) {
        s.started = 1;
        // Keys that arrived together (a sequenced chord) play at once. A key that
        // arrived alone waits about 15 ms for the rest of a hand-played chord, so
        // that the first note is the right one and nothing is struck twice. The
        // steps still count from the first key.
        if (count_bits(s.active) >= 2) {
            play_step(s, p);
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
        play_step(s, p);
        return;
    }
    if (s.gate_left && --s.gate_left == 0) silence(s);
    if (++s.rearm >= kRearmInterval) {
        s.rearm = 0;
        mark(s, static_cast<u8>(s.active & ~s.sounding));
    }
    if (++s.tick < p.rate) return;
    s.tick = 0;
    play_step(s, p);
}

inline u32 lowest_bit(u32 mask) {
    u32 i = 0;
    while (!(mask >> i & 1)) ++i;
    return i;
}

// A held key's silent placeholder. Velocity 0: on the device a placeholder at
// velocity 1 was heard briefly when keys were pressed together, so it must be
// silent even if it sounds.
inline void park(Tone& tone, u32 root) {
    tone.pitch = static_cast<u8>(root);
    tone.velocity = 1;
    tone.delay = kParkedDelay;
}

bool press(State& s, const Controls& c, u32 note, u32 velocity, Plan& plan) {
    plan.count = 0;
    if (note > 127) return false;
    const Params p = read_params(c);
    s.latch_on = p.latch;
    if (all_off(p)) {  // every slot OFF: the instrument plays the note itself
        s.pass_through[note >> 5] |= 1u << (note & 31);
        return false;
    }
    s.event_pending = 1;
    u32 slot = find_slot(s, note);
    // Latch: a key pressed after all keys were let go replaces the latched set.
    const bool replacing = p.latch && !s.held && s.active;
    const bool phrase = !s.held && (replacing || !s.active);
    if (replacing) let_go(s, static_cast<u8>(s.active & ~(slot < kSlots ? 1u << slot : 0u)));
    if (slot == kSlots) {
        const u8 used = static_cast<u8>(s.active | s.releasing);
        if (used == 0xff) return true;  // the SDK cannot hold a ninth root
        slot = lowest_bit(static_cast<u8>(~used));
    }
    const u8 bit = static_cast<u8>(1u << slot);
    Key& key = s.keys[slot];
    key.root = static_cast<u8>(note);
    key.velocity = static_cast<u8>(velocity < 1 ? 1 : velocity > 127 ? 127 : velocity);
    key.order = ++s.order_counter;
    s.held |= bit;
    s.active |= bit;
    s.releasing &= static_cast<u8>(~bit);
    forget(s, bit);  // the plan replaces anything this root held
    s.pool_dirty = 1;
    plan.count = 1;
    park(plan.tones[0], note);
    if (phrase) start_phrase(s, p);
    return true;
}

bool release(State& s, u32 note) {
    if (note < 128 && (s.pass_through[note >> 5] >> (note & 31) & 1)) {
        s.pass_through[note >> 5] &= ~(1u << (note & 31));
        return false;  // the instrument's own note
    }
    s.event_pending = 1;
    const u32 slot = find_slot(s, note);
    if (slot == kSlots || !(s.active >> slot & 1)) return true;  // a key press() could not hold
    const u8 bit = static_cast<u8>(1u << slot);
    s.held &= static_cast<u8>(~bit);
    // Latch keeps the key: its record stays because stock does not release it.
    // At most seven, so that a new key can always replace the set.
    if (s.latch_on && count_bits(s.active) < kSlots) return true;
    s.active &= static_cast<u8>(~bit);
    forget(s, bit);
    s.pool_dirty = 1;
    if (!s.active) s.started = 0;
    return false;  // stock releases this root's notes
}

bool update(State& s, const Controls& c, Update& change) {
    if (!s.draining) {
        s.draining = 1;
        s.drain_calls = 0;
        if (s.event_pending) s.event_pending = 0;
        else clock_step(s, c);
    }
    s.dirty &= s.active;
    if (!s.dirty && !s.releasing) {
        s.draining = 0;
        return false;
    }
    change.mode = opxy::player::Replace;
    if (s.releasing) {  // a latched key let go: an empty plan releases its root
        const u32 slot = lowest_bit(s.releasing);
        s.releasing &= static_cast<u8>(~(1u << slot));
        change.root = s.keys[slot].root;
        change.plan.count = 0;
    } else {
        const u32 slot = lowest_bit(s.dirty);
        s.dirty &= static_cast<u8>(~(1u << slot));
        change.root = s.keys[slot].root;
        change.plan.count = 1;
        if (s.chord_carrier == slot + 1 && s.chord_count) {
            change.plan.count = s.chord_count;  // stock sounds a record's notes in this order
            for (u32 i = 0; i < s.chord_count; ++i) {
                change.plan.tones[i].pitch = s.chord_pitch[i];
                change.plan.tones[i].velocity = s.chord_velocity[i];
                change.plan.tones[i].delay = 0;
            }
        } else if (s.sounding >> slot & 1) {
            change.plan.tones[0].pitch = s.key_pitch[slot];
            change.plan.tones[0].velocity = s.keys[slot].velocity;
            change.plan.tones[0].delay = 0;
        } else {
            park(change.plan.tones[0], change.root);
        }
    }
    if (++s.drain_calls >= kDrainLimit) s.draining = 0;  // the SDK stops calling here
    return true;
}

// Stock reset and all-notes-off: stock has already silenced every record.
void reset(State& s) { __builtin_memset(&s, 0, sizeof s); }

// ---- Screen ----

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

void display(const State& s, const Controls& c, Display& d) {
    // Lower case: a header value has under 60 px.
    static const char names[kPatterns][6] = {"off", "up", "down", "up/dn", "dn/up", "updn+", "dnup+", "order", "conv", "divrg",
                                             "pinky", "thumb", "stu+", "stu-", "std-", "std+", "shuf", "rand", "drunk", "chord", "chd/1"};
    static const char rates[kRates][6] = {"1/2t", "1/4", "1/4t", "1/8", "1/8t", "1/16", "1/16t", "1/32", "1/32t"};
    static const char notes[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const Params p = read_params(c);
    u32 at;
    for (u32 i = 0; i < kChain; ++i) {
        at = 0;
        append(d.values[i], sizeof d.values[i], at, names[p.pattern[i]]);
    }
    at = 0;
    append(d.values[4], sizeof d.values[4], at, rates[opxy::player::choice(c.value[4], kRates)]);
    at = 0;
    append_number(d.values[5], sizeof d.values[5], at, p.gate);
    append(d.values[5], sizeof d.values[5], at, "%");
    at = 0;
    append_number(d.values[6], sizeof d.values[6], at, p.octaves);
    at = 0;
    append_number(d.values[7], sizeof d.values[7], at, p.cycles);
    if (p.latch) append(d.values[7], sizeof d.values[7], at, "L");

    // Dots 0..3 mark the playing slot under its header column; dots 4..11 count
    // the slot's cycles; dots 12..23 fill as the current cycle plays.
    at = 0;
    const bool running = s.active && s.started && s.has_note && s.shown_length;
    if (!running) {
        for (u32 i = 0; i < p.cycles; ++i) d.lit |= 1u << (4 + i);
        append(d.text, sizeof d.text, at, all_off(p) ? "all off" : "ready");
        return;
    }
    d.lit |= 1u << (s.shown_slot & (kChain - 1));
    for (u32 i = 0; i <= s.shown_cycle && i < p.cycles; ++i) d.lit |= 1u << (4 + i);
    const u32 filled = divide((s.shown_step + 1) * kProgressDots + s.shown_length - 1, s.shown_length);
    for (u32 i = 0; i < filled && i < kProgressDots; ++i) d.lit |= 1u << (12 + i);
    append(d.text, sizeof d.text, at, "arp ");
    append_number(d.text, sizeof d.text, at, s.shown_slot + 1u);
    append(d.text, sizeof d.text, at, "  ");
    if (s.shown_chord) {
        append(d.text, sizeof d.text, at, "chord");
        return;
    }
    const u32 octave = divide(s.shown_pitch, 12);
    append(d.text, sizeof d.text, at, notes[s.shown_pitch - 12 * octave]);
    if (octave) append_number(d.text, sizeof d.text, at, octave - 1);
    else append(d.text, sizeof d.text, at, "-1");
}
}  // namespace chained_arp
