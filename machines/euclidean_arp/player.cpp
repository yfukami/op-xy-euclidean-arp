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

namespace euclidean_arp {
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

enum Style { Up, Down, UpDown, Order, Random, Chord };

struct Slot {
    u8 root, velocity, next_pitch, next_velocity;
    u32 order;
};

struct State {
    Slot slots[kSlots];
    u8 pool_pitch[kPool], pool_slot[kPool];
    u32 pool_count, pool_style, pool_octaves, order_counter;
    u32 step, tick, gate_left, rearm, cursor, chord_octave, rng;
    u8 active, sounding, off_mask, on_mask;
    u8 pool_dirty, accent_ready, started, event_pending;
    u8 draining, drain_calls, has_note, shown_pitch, descending;
};

struct Params {
    u32 rate, style, octaves, gate, steps, hits, rotate;
    i32 accent;
};

inline u32 smaller(u32 a, u32 b) { return a < b ? a : b; }
inline u32 divide(u32 n, u32 d) { return opxy::divide::quotient16(n, d); }
inline u32 modulo(u32 n, u32 d) { return n - d * divide(n, d); }

inline Params read_params(const Controls& c) {
    using opxy::player::choice;
    static const u16 rates[4] = {240, 120, 60, 30};
    Params p;
    p.rate = rates[choice(c.value[0], 4)];
    p.style = choice(c.value[1], 6);
    p.octaves = 1 + choice(c.value[2], kOctaves);
    p.gate = 10 + 5 * choice(c.value[3], 19);
    p.steps = 1 + choice(c.value[4], kMaxSteps);
    p.hits = smaller(choice(c.value[5], kMaxSteps + 1), p.steps);
    p.rotate = smaller(choice(c.value[6], kMaxSteps), p.steps - 1);
    p.accent = 5 * static_cast<i32>(choice(c.value[7], 41)) - 100;
    return p;
}

// Maximally even, equal to Bjorklund's pattern up to rotation; constant time.
inline bool is_hit(u32 step, u32 steps, u32 hits, u32 rotate) {
    if (!hits) return false;
    return modulo(modulo(step + steps - rotate, steps) * hits, steps) < hits;
}

inline u32 accented(u32 velocity, i32 accent) {
    u32 v = velocity;
    if (accent > 0) v += divide((127 - v) * static_cast<u32>(accent), 100);
    else if (accent < 0) v -= divide(v * static_cast<u32>(-accent), 100);
    return v < 1 ? 1 : v > 127 ? 127 : v;
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

// After the pool changes, continue from the note last played, so that a key
// joining the arpeggio neither repeats that note nor skips one.
inline void anchor_cursor(State& s, u32 style) {
    const u32 n = s.pool_count;
    if (!s.has_note || !n || style == Random || style == Chord) return;
    const u32 last = s.shown_pitch;
    if (style == Order) {
        for (u32 i = 0; i < n; ++i)
            if (s.pool_pitch[i] == last) s.cursor = i + 1;
        return;
    }
    u32 below = 0;
    while (below < n && s.pool_pitch[below] < last) ++below;
    const u32 above = below < n && s.pool_pitch[below] == last ? below + 1 : below;
    if (style == Up) {
        s.cursor = above;
    } else if (style == Down) {
        s.cursor = below ? n - below : 0;
    } else if (s.descending && n > 1) {
        s.cursor = below ? 2 * n - 2 - (below - 1) : 1;
    } else {
        s.cursor = above;
    }
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
    anchor_cursor(s, p.style);
}

inline u32 next_random(State& s) {
    u32 x = s.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s.rng = x;
    return x;
}

inline u32 next_pool_index(State& s, u32 style) {
    const u32 n = s.pool_count;
    if (style == Random) return ((next_random(s) >> 16) * n) >> 16;
    const u32 period = style == UpDown && n > 1 ? 2 * n - 2 : n;
    if (s.cursor >= period) s.cursor = 0;
    const u32 at = s.cursor++;
    if (style == Down) return n - 1 - at;
    s.descending = at >= n;
    return at < n ? at : period - at;
}

inline void queue_note(State& s, u32 slot, u32 pitch, u32 velocity) {
    const u8 bit = static_cast<u8>(1u << slot);
    s.slots[slot].next_pitch = static_cast<u8>(pitch);
    s.slots[slot].next_velocity = static_cast<u8>(velocity);
    s.on_mask |= bit;
    s.off_mask &= static_cast<u8>(~bit);  // its Replace already ends the old note
}

inline void trigger(State& s, const Params& p) {
    s.off_mask |= s.sounding;
    if (s.pool_dirty || s.pool_style != p.style || s.pool_octaves != p.octaves) rebuild_pool(s, p);
    const i32 accent = s.accent_ready ? p.accent : 0;
    bool any = false;
    u32 lowest = 127;
    if (p.style == Chord) {
        if (s.chord_octave >= p.octaves) s.chord_octave = 0;
        const u32 shift = 12 * s.chord_octave++;
        for (u32 i = 0; i < kSlots; ++i) {
            if (!(s.active >> i & 1) || s.slots[i].root + shift > 127) continue;
            const u32 pitch = s.slots[i].root + shift;
            queue_note(s, i, pitch, accented(s.slots[i].velocity, accent));
            lowest = smaller(lowest, pitch);
            any = true;
        }
    } else if (s.pool_count) {
        const u32 at = next_pool_index(s, p.style);
        const u32 slot = s.pool_slot[at];
        lowest = s.pool_pitch[at];
        queue_note(s, slot, lowest, accented(s.slots[slot].velocity, accent));
        any = true;
    }
    s.accent_ready = 0;
    if (!any) return;
    s.shown_pitch = static_cast<u8>(lowest);
    s.has_note = 1;
    const u32 gate = divide(p.rate * p.gate, 100);
    s.gate_left = gate ? gate : 1;
}

inline void clock_step(State& s, const Controls& c) {
    if (!s.active) return;
    if (!s.started) {  // the step that plays the press's own note
        s.started = 1;
        return;
    }
    const Params p = read_params(c);
    if (s.gate_left && --s.gate_left == 0) s.off_mask |= s.sounding;
    if (++s.rearm >= kRearmInterval) {
        s.rearm = 0;
        s.off_mask |= static_cast<u8>(s.active & ~s.sounding);
    }
    if (++s.tick < p.rate) return;
    s.tick = 0;
    if (++s.step >= p.steps) {
        s.step = 0;
        s.accent_ready = 1;
    }
    if (is_hit(s.step, p.steps, p.hits, p.rotate)) trigger(s, p);
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

bool press(State& s, const Controls& c, u32 note, u32 velocity, Plan& plan) {
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
    s.off_mask &= static_cast<u8>(~bit);
    s.on_mask &= static_cast<u8>(~bit);
    s.pool_dirty = 1;
    plan.count = 1;
    park(plan.tones[0], note);
    if (!first) return true;

    s.step = s.tick = s.gate_left = s.rearm = s.cursor = s.chord_octave = 0;
    s.started = s.has_note = s.descending = 0;
    s.accent_ready = 1;
    if (!s.rng) s.rng = 0x9e3779b9u ^ (note << 8) ^ key.velocity;
    const Params p = read_params(c);
    if (is_hit(0, p.steps, p.hits, p.rotate)) trigger(s, p);
    if (s.on_mask & bit) {
        plan.tones[0].pitch = key.next_pitch;
        plan.tones[0].velocity = key.next_velocity;
        plan.tones[0].delay = 0;
        s.on_mask &= static_cast<u8>(~bit);
        s.sounding |= bit;
    }
    s.off_mask = 0;
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
    static const char rates[4][5] = {"1/4", "1/8", "1/16", "1/32"};
    static const char styles[6][6] = {"UP", "DOWN", "UP/DN", "ORDER", "RAND", "CHORD"};
    static const char notes[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const Params p = read_params(c);
    u32 at = 0;
    append(d.values[0], sizeof d.values[0], at, rates[opxy::player::choice(c.value[0], 4)]);
    at = 0;
    append(d.values[1], sizeof d.values[1], at, styles[p.style]);
    show_number(d.values[2], p.octaves, "");
    show_number(d.values[3], p.gate, "%");
    show_number(d.values[4], p.steps, "");
    show_number(d.values[5], p.hits, "");
    show_number(d.values[6], p.rotate, "");
    at = 0;
    if (p.accent) append(d.values[7], sizeof d.values[7], at, p.accent > 0 ? "+" : "-");
    append_number(d.values[7], sizeof d.values[7], at, static_cast<u32>(p.accent < 0 ? -p.accent : p.accent));
    append(d.values[7], sizeof d.values[7], at, "%");

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
}  // namespace euclidean_arp
