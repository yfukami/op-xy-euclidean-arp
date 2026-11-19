#include <cstring>
#include "harness.hpp"

enum { R4, R8, R16, R32 };
static const u32 kRateSteps[4] = {240, 120, 60, 30};
static const u32 kStart = 0;  // the first note sounds on the next player step
typedef std::vector<u32> Notes;

static std::vector<int> bjorklund(u32 hits, u32 steps) {
    typedef std::vector<int> Group;
    std::vector<Group> a(hits, Group(1, 1)), b(steps - hits, Group(1, 0));
    if (a.empty()) a.swap(b);
    while (b.size() > 1) {
        const size_t m = a.size() < b.size() ? a.size() : b.size();
        std::vector<Group> merged, rest;
        for (size_t i = 0; i < m; ++i) {
            Group g = a[i];
            g.insert(g.end(), b[i].begin(), b[i].end());
            merged.push_back(g);
        }
        rest.assign(a.size() > m ? a.begin() + m : b.begin() + m, a.size() > m ? a.end() : b.end());
        a.swap(merged);
        b.swap(rest);
    }
    Group out;
    for (const Group& g : a) out.insert(out.end(), g.begin(), g.end());
    for (const Group& g : b) out.insert(out.end(), g.begin(), g.end());
    return out;
}

static bool same_necklace(const std::vector<int>& a, const std::vector<int>& b) {
    if (a.size() != b.size()) return false;
    for (size_t r = 0; r < a.size(); ++r) {
        bool same = true;
        for (size_t i = 0; i < a.size() && same; ++i) same = a[i] == b[(i + r) % b.size()];
        if (same) return true;
    }
    return false;
}

static void euclid() {
    for (u32 n = 1; n <= 16; ++n)
        for (u32 k = 0; k <= n; ++k) {
            std::vector<int> ours;
            u32 count = 0;
            for (u32 i = 0; i < n; ++i) {
                ours.push_back(arp::is_hit(i, n, k, 0));
                count += arp::is_hit(i, n, k, 0);
            }
            require(count == k);
            require(same_necklace(ours, bjorklund(k, n)));
            if (k) require(ours[0]);
            for (u32 r = 0; r < n; ++r)
                for (u32 i = 0; i < n; ++i)
                    require(arp::is_hit((i + r) % n, n, k, r) == (ours[i] != 0));
        }
}

static void parameters_clamp() {
    Host h;
    h.configure(R16, arp::Up, 1, 50, 4, 16, 15, 100);
    const arp::Params p = arp::read_params(h.controls);
    require(p.steps == 4 && p.hits == 4 && p.rotate == 3 && p.rate == 60 && p.gate == 50 && p.chance == 100 && !p.keep_first);
    h.configure(R32, arp::Chord, 4, 100, 16, 0, 0, 0, true);
    const arp::Params q = arp::read_params(h.controls);
    require(q.rate == 30 && q.style == arp::Chord && q.octaves == 4 && q.gate == 100 && q.hits == 0 && q.chance == 0 && q.keep_first);
}

static Notes first_notes(u32 style, const Notes& keys, u32 count) {
    Host h;
    h.configure(R32, style, 1, 50, 1, 1, 0, 100);
    for (u32 key : keys) h.press(key);
    h.step(30 * count);
    return h.pitches();
}

static void chord_sets_the_first_note() {
    // Whatever order a chord's keys arrive in, the style decides the first note.
    const Notes orders[6] = {{60, 64, 67}, {60, 67, 64}, {64, 60, 67}, {64, 67, 60}, {67, 60, 64}, {67, 64, 60}};
    for (const Notes& keys : orders) {
        require(first_notes(arp::Up, keys, 4) == Notes({60, 64, 67, 60}));
        require(first_notes(arp::UpDown, keys, 6) == Notes({60, 64, 67, 64, 60, 64}));
        require(first_notes(arp::DownUp, keys, 6) == Notes({67, 64, 60, 64, 67, 64}));
        require(first_notes(arp::Down, keys, 4) == Notes({67, 64, 60, 67}));
    }
    require(first_notes(arp::Order, {67, 60, 64}, 4) == Notes({67, 60, 64, 67}));

    // A later key inside the window replaces the first note; after it, a key just joins.
    Host spread;
    spread.configure(R16, arp::Up, 1, 50, 1, 1, 0, 100);
    spread.press(67);
    spread.step(4);
    spread.press(64);
    spread.step(4);
    spread.press(60);
    spread.step(2);
    require(spread.pitches() == Notes({67, 64, 60}) && spread.ons()[2].time == 8 && spread.sounding() == 1);
    spread.press(55);
    spread.step(120);
    require(spread.pitches() == Notes({67, 64, 60, 64, 67}));

    // The right first note is not struck twice, and a chord only adds the late key.
    Host same;
    same.configure(R16, arp::Up, 1, 50, 1, 1, 0, 100);
    same.press(60);
    same.step(2);
    same.press(64);
    same.step(10);
    require(same.pitches() == Notes({60}) && same.sounding() == 1);
    Host joined;
    joined.configure(R16, arp::Chord, 1, 50, 1, 1, 0, 100);
    joined.press(60);
    joined.step(2);
    joined.press(64);
    joined.step(10);
    require(joined.pitches() == Notes({60, 64}) && joined.sounding() == 2);

    // A key after the window never changes the note that is playing.
    Host late;
    late.configure(R16, arp::Up, 1, 50, 1, 1, 0, 100);
    late.press(67);
    late.step(arp::kDefaultChordWindow + 1);
    late.press(60);
    late.step(10);
    require(late.pitches() == Notes({67}));

    // A first hit CHANCE skipped stays silent when a key lands late.
    Host skipped;
    skipped.configure(R16, arp::Up, 1, 50, 1, 1, 0, 0);
    skipped.press(67);
    skipped.step(2);
    skipped.press(60);
    skipped.step(10);
    require(skipped.events.empty());

    require(arp::chord_window(1200, 240) == 9 && arp::chord_window(600, 240) == 4);
    require(arp::chord_window(3000, 30) == 15 && arp::chord_window(200, 240) == 1 && arp::chord_window(0, 240) == 9);
}

static void step_sequenced_chord() {
    // A chord on one sequencer step: every note arrives before the next player step.
    const Notes orders[6] = {{60, 64, 67}, {60, 67, 64}, {64, 60, 67}, {64, 67, 60}, {67, 60, 64}, {67, 64, 60}};
    for (const Notes& keys : orders) {
        Host h;
        h.configure(R16, arp::Up, 1, 50, 16, 16, 0, 100);
        for (u32 loop = 0; loop < 4; ++loop) {
            const size_t before = h.ons().size();
            for (u32 key : keys) h.press(key);
            h.step(240);
            require(h.ons().size() == before + 4);
            require(h.ons()[before].pitch == 60 && h.ons()[before].time == h.now - 240 + kStart);
            require(h.ons()[before + 1].pitch == 64 && h.ons()[before + 2].pitch == 67);
            // The note ends and the same chord returns on the same sequencer tick.
            for (u32 key : keys) h.release(key);
            require(h.sounding() == 0);
        }
        require(h.stale_updates == 0);
    }

    // Lowest note first also holds with octaves, and when the first step is a rest.
    Host octaves;
    octaves.configure(R16, arp::Up, 2, 50, 8, 8, 0, 100);
    octaves.press(67); octaves.press(64); octaves.press(60);
    octaves.step(200);
    require(octaves.pitches() == Notes({60, 64, 67, 72}));
    Host rotated;
    rotated.configure(R16, arp::Up, 1, 50, 8, 1, 1, 100);
    rotated.press(67); rotated.press(64); rotated.press(60);
    rotated.step(61);
    require(rotated.pitches() == Notes({60}) && rotated.ons()[0].time == 60);
}

static void first_note_is_prompt() {
    Host h;
    h.configure(R16, arp::Up, 1, 50, 8, 3, 0, 100);
    h.press(60, 90);
    h.step(kStart);
    require(h.events.empty());
    h.step();
    require(h.ons().size() == 1 && h.ons()[0].time == kStart && h.ons()[0].pitch == 60 && h.ons()[0].velocity == 90);

    Host rest;
    rest.configure(R16, arp::Up, 1, 50, 8, 1, 1, 100);  // the only hit is on step 1
    rest.press(60);
    rest.step(60);
    require(rest.ons().empty());
    rest.step();
    require(rest.ons().size() == 1 && rest.ons()[0].time == 60);
}

static void rates_and_event_drains() {
    for (u32 rate = 0; rate < 4; ++rate) {
        Host h;
        h.configure(rate, arp::Up, 1, 50, 1, 1, 0, 100);
        h.press(60);
        h.step(kRateSteps[rate] * 4 + 1);
        require(h.ons().size() == 5);
        for (u32 i = 0; i < 5; ++i) require(h.ons()[i].time == (i ? i * kRateSteps[rate] : kStart));
    }
    // Key events between steps do not move the clock.
    Host h;
    h.configure(R16, arp::Up, 1, 50, 1, 1, 0, 100);
    h.press(60);
    h.step(10);
    for (u32 i = 0; i < 20; ++i) { h.press(72); h.release(72); }
    h.step(51);
    require(h.ons().size() == 2 && h.ons()[1].time == 60);
}

static void gates() {
    Host tenth;
    tenth.configure(R32, arp::Up, 1, 10, 1, 1, 0, 100);
    tenth.press(60);
    tenth.step(20);
    require(tenth.events.size() == 2 && !tenth.events[1].on && tenth.events[1].time == kStart + 3);

    Host half;
    half.configure(R16, arp::Up, 1, 50, 2, 1, 0, 100);
    half.press(60);
    half.step(200);
    require(half.events[1].time == kStart + 30 && !half.events[1].on);
    require(half.ons()[1].time == 120);

    // 100%: the note ends exactly as the next begins, and never overlaps it.
    Host full;
    full.configure(R16, arp::Up, 1, 100, 1, 1, 0, 100);
    full.press(60);
    full.press(64);
    for (u32 i = 0; i < 200; ++i) { full.step(); require(full.sounding() <= 1); }
    require(full.events[1].time == 60 && !full.events[1].on && full.events[1].pitch == 60);
    require(full.events[2].time == 60 && full.events[2].on && full.events[2].pitch == 64);

    // A faster RATE brings the next hit before the old gate ends: the old note still stops.
    Host faster;
    faster.configure(R4, arp::Up, 1, 100, 1, 1, 0, 100);
    faster.press(60);
    faster.press(64);
    faster.step(10);
    faster.configure(R32, arp::Up, 1, 100, 1, 1, 0, 100);
    faster.step(21);
    require(faster.ons().size() == 2 && faster.ons()[1].pitch == 64 && faster.sounding() == 1);
}

static void styles() {
    Host up;
    up.configure(R32, arp::Up, 2, 50, 1, 1, 0, 100);
    up.press(64); up.press(60);
    up.step(30 * 5);
    require(up.pitches() == Notes({60, 64, 72, 76, 60}));

    Host down;
    down.configure(R32, arp::Down, 1, 50, 1, 1, 0, 100);
    down.press(60); down.press(64); down.press(67);
    down.step(30 * 5);
    require(down.pitches() == Notes({67, 64, 60, 67, 64}));

    Host updown;
    updown.configure(R32, arp::UpDown, 1, 50, 1, 1, 0, 100);
    updown.press(60); updown.press(64); updown.press(67);
    updown.step(30 * 8);
    require(updown.pitches() == Notes({60, 64, 67, 64, 60, 64, 67, 64}));

    // A key joining DN/UP keeps the direction it was travelling in.
    Host joining;
    joining.configure(R32, arp::DownUp, 1, 50, 1, 1, 0, 100);
    joining.press(60); joining.press(64); joining.press(67);
    joining.step(31);
    joining.press(72);
    joining.step(120);
    require(joining.pitches() == Notes({67, 64, 60, 64, 67, 72}));

    // Two notes alternate, and one note repeats.
    require(first_notes(arp::DownUp, {60, 67}, 4) == Notes({67, 60, 67, 60}));
    require(first_notes(arp::DownUp, {60}, 3) == Notes({60, 60, 60}));

    // Projects saved with six styles keep their style.
    const u32 kept[6] = {arp::Up, arp::Down, arp::UpDown, arp::Order, arp::Random, arp::Chord};
    for (u32 i = 0; i < 6; ++i) {
        const u32 width = 0x7fffffffu / 6;
        require(player::choice(static_cast<i32>(i * width + width / 2), arp::kStyles) == kept[i]);
    }

    Host order;
    order.configure(R32, arp::Order, 2, 50, 1, 1, 0, 100);
    order.press(67); order.press(60);
    order.step(30 * 5);
    require(order.pitches() == Notes({67, 60, 79, 72, 67}));

    Host random;
    random.configure(R32, arp::Random, 1, 50, 1, 1, 0, 100);
    random.press(60); random.press(64); random.press(67);
    random.step(30 * 300);
    u32 seen[3] = {0, 0, 0};
    for (u32 pitch : random.pitches()) {
        require(pitch == 60 || pitch == 64 || pitch == 67);
        ++seen[pitch == 60 ? 0 : pitch == 64 ? 1 : 2];
    }
    require(seen[1] > 50 && seen[2] > 50);

    // Keys an octave apart overlap across octaves: each pitch appears once.
    Host unique;
    unique.configure(R32, arp::Up, 2, 50, 1, 1, 0, 100);
    unique.press(60); unique.press(72);
    unique.step(30 * 4);
    require(unique.pitches() == Notes({60, 72, 84, 60}));

    // Nothing above MIDI 127.
    Host top;
    top.configure(R32, arp::Up, 4, 50, 1, 1, 0, 100);
    top.press(120);
    top.step(30 * 4);
    require(top.pitches() == Notes({120, 120, 120, 120}));
}

static void chord_of_eight() {
    Host h;
    h.configure(R16, arp::Chord, 2, 100, 1, 1, 0, 100);
    for (u32 i = 0; i < 8; ++i) h.press(60 + i);
    h.step(61);
    // The first hit plays all eight keys; the next plays them an octave up.
    require(h.ons().size() == 16 && h.sounding() == 8);
    for (u32 i = 0; i < 8; ++i) {
        require(h.ons()[i].time == kStart && h.ons()[i].pitch == 60 + i);
        require(h.ons()[8 + i].time == 60 && h.ons()[8 + i].pitch == 72 + i);
    }
    require(h.largest_drain == 8);
    // A full drain must not disturb the clock.
    h.step(60);
    require(h.ons().size() == 24 && h.ons()[23].time == 120 && h.ons()[16].pitch == 60);
    require(h.stale_updates == 0);
}

static void chance() {
    // The encoder runs 0..100%, then on through 95*..0*; the centre is the old default.
    Host h;
    const u32 amounts[5][3] = {{0, 0, 0}, {20, 100, 0}, {21, 95, 1}, {36, 20, 1}, {40, 0, 1}};
    for (const auto& a : amounts) {
        h.set(7, a[0], 41);
        const arp::Params p = arp::read_params(h.controls);
        require(p.chance == a[1] && p.keep_first == (a[2] != 0));
    }

    Host all;
    all.configure(R32, arp::Up, 1, 50, 4, 2, 0, 100);
    all.press(60, 100);
    all.step(240);
    require(all.ons().size() == 4);
    for (const Event& on : all.ons()) require(on.velocity == 100);

    Host none;
    none.configure(R32, arp::Up, 1, 50, 4, 2, 0, 0);
    none.press(60);
    none.step(1000);
    require(none.events.empty());

    // 0*: only each loop's first hit plays, and skipped hits still move the arpeggio on.
    Host first;
    first.configure(R32, arp::Up, 1, 50, 4, 2, 0, 0, true);
    first.press(60); first.press(64); first.press(67);
    first.step(250);
    require(first.pitches() == Notes({60, 67, 64}));
    require(first.ons()[0].time == kStart && first.ons()[1].time == 120 && first.ons()[2].time == 240);

    // A key joining after a skipped hit continues from the skipped note.
    Host joined;
    joined.configure(R32, arp::Up, 1, 50, 4, 2, 0, 0, true);
    joined.press(60); joined.press(64); joined.press(67);
    joined.step(61);  // 60 played, 64 skipped
    joined.press(72);
    joined.step(189);
    require(joined.pitches() == Notes({60, 67, 60}));

    // A skipped chord moves on an octave too, so the first hit stays at the bottom here.
    Host chord;
    chord.configure(R32, arp::Chord, 2, 50, 2, 2, 0, 0, true);
    chord.press(60); chord.press(64);
    chord.step(300);
    require(chord.ons().size() == 10);
    for (u32 pitch : chord.pitches()) require(pitch == 60 || pitch == 64);

    Host fifth;
    fifth.configure(R32, arp::Up, 1, 50, 1, 1, 0, 20);
    fifth.press(60);
    fifth.step(30 * 5000);
    require(fifth.ons().size() > 800 && fifth.ons().size() < 1200);
    require(fifth.sounding() <= 1);

    Host half;
    half.configure(R32, arp::Up, 1, 50, 2, 2, 0, 50, true);
    half.press(60);
    half.step(60 * 2000);
    u32 firsts = 0, others = 0;
    for (const Event& on : half.ons()) {
        if (on.time == kStart || on.time % 60 == 0) ++firsts;
        else ++others;
    }
    require(firsts == 2000 && others > 800 && others < 1200);
}

static void cleanup() {
    Host h;
    h.configure(R16, arp::Up, 2, 100, 1, 1, 0, 100);
    h.press(60); h.press(64);
    h.step(10);
    require(h.sounding() == 1);
    h.release(60);  // its own note stops; the other key carries on
    require(h.sounding() == 0);
    h.step(60);
    require(h.sounding() == 1);
    h.release(64);
    require(h.sounding() == 0 && h.state.active == 0);
    h.step(500);
    require(h.sounding() == 0 && h.stale_updates == 0);

    // A new first press restarts the pattern at step 0.
    h.configure(R16, arp::Up, 1, 50, 8, 8, 0, 100);
    const size_t before = h.ons().size();
    h.press(62);
    require(h.state.step == 0);
    h.step(kStart + 1);
    require(h.ons().size() == before + 1 && h.ons().back().pitch == 62);
    h.all_notes_off();
    require(h.sounding() == 0 && h.state.active == 0);
    h.step(200);
    require(h.sounding() == 0 && h.stale_updates == 0);

    Host full;
    full.configure(R16, arp::Up, 1, 50, 1, 1, 0, 100);
    for (u32 i = 0; i < 9; ++i) full.press(60 + i);
    require(full.passed_through == 0 && full.held == 8 && !full.find(68));
    full.release(68);  // the untracked key changes nothing
    require(full.state.active == 0xff);
    for (u32 i = 0; i < 8; ++i) full.release(60 + i);
    full.step(200);
    require(full.sounding() == 0 && full.stale_updates == 0);

    // The same key again keeps one record and one slot.
    Host again;
    again.configure(R16, arp::Up, 1, 50, 1, 1, 0, 100);
    again.press(60); again.step(5); again.press(60);
    require(again.sounding() == 0 && again.state.active == 1);
    again.step(120);
    again.release(60);
    require(again.sounding() == 0 && again.state.active == 0);
}

static void parked_tones_never_sound() {
    Host h;
    h.configure(R32, arp::Up, 1, 50, 16, 0, 0, 100);
    h.press(60); h.press(64);
    h.step(200000);
    require(h.events.empty() && h.stale_updates == 0 && h.state.active == 3);
    // After that long silence the keys still play.
    h.configure(R32, arp::Up, 1, 50, 1, 1, 0, 100);
    h.step(31);
    require(h.ons().size() >= 1);

    // Queued tones of keys that join a playing pattern stay dark too.
    Host joined;
    joined.configure(R4, arp::Up, 1, 10, 16, 1, 0, 100);
    joined.press(60);
    joined.step(100);
    joined.press(64);
    joined.step(70000);
    for (const Event& on : joined.ons()) require(on.velocity != 1);  // parked tones have velocity 1
    require(joined.ons().size() > 2 && joined.stale_updates == 0);
}

static void live_edits() {
    Host h;
    h.configure(R32, arp::Up, 1, 50, 16, 16, 0, 100);
    h.press(60);
    h.step(30 * 10);
    require(h.state.step == 9);
    h.configure(R32, arp::Up, 1, 50, 4, 4, 0, 100);  // the playhead wraps instead of running off
    h.step(30);
    require(h.state.step == 0);
}

static bool terminated(const char* text, u32 size) { return std::memchr(text, 0, size) != nullptr; }

static void screen() {
    Host h;
    h.configure(R16, arp::UpDown, 3, 55, 8, 3, 0, 25, true);
    player::Display d = {};
    arp::display(h.state, h.controls, d);
    const char* const expected[8] = {"1/16", "up/dn", "3", "55%", "8", "3", "0", "25%*"};
    for (u32 i = 0; i < 8; ++i) {
        require(terminated(d.values[i], sizeof d.values[i]));
        require(std::strcmp(d.values[i], expected[i]) == 0);
    }
    require(std::strcmp(d.text, "3/8") == 0);
    require(d.lit == ((1u << 0) | (1u << 3) | (1u << 6)));  // no playhead without keys

    h.configure(R16, arp::UpDown, 3, 55, 8, 3, 0, 100);  // every hit plays, so the note is known
    h.press(61);
    for (u32 i = 0; i < 60 * 3 + 1; ++i) h.step();
    player::Display on = {};
    arp::display(h.state, h.controls, on);
    require(h.state.step == 3 && std::strcmp(on.text, "C#5") == 0);  // 61, then 73 going up
    require((on.lit & 0xffffu) == d.lit);
    require(((on.lit >> 16) & 0xfu) == (1u << 3));         // tick 3 within its quadrant
    require(((on.lit >> 20) & 0xfu) == (0xfu & ~1u));      // only quadrant 0 stays uncovered

    // Every playhead position leaves exactly one tick uncovered.
    for (u32 step = 0; step < 16; ++step) {
        h.state.step = step;
        player::Display shown = {};
        arp::display(h.state, h.controls, shown);
        u32 visible = 0, slot = 0;
        for (u32 r = 0; r < 4; ++r)
            for (u32 q = 0; q < 4; ++q)
                if ((shown.lit >> (16 + r) & 1) && !(shown.lit >> (20 + q) & 1)) { ++visible; slot = 4 * q + r; }
        require(visible == 1 && slot == step);
    }

    Host extremes;
    extremes.configure(R4, arp::Chord, 4, 100, 16, 16, 15, 95, true);
    player::Display wide = {};
    arp::display(extremes.state, extremes.controls, wide);
    require(std::strcmp(wide.values[7], "95%*") == 0 && std::strcmp(wide.values[3], "100%") == 0);
    require(std::strcmp(wide.text, "16/16") == 0 && (wide.lit & 0xffffu) == 0xffffu);
    extremes.configure(R4, arp::Up, 1, 10, 1, 0, 0, 0);
    player::Display low = {};
    arp::display(extremes.state, extremes.controls, low);
    require(std::strcmp(low.values[7], "0%") == 0 && low.lit == 0 && std::strcmp(low.text, "0/1") == 0);
    arp::State low_note = {};
    low_note.active = 1; low_note.has_note = 1; low_note.shown_pitch = 0;
    player::Display bottom = {};
    arp::display(low_note, extremes.controls, bottom);
    require(std::strcmp(bottom.text, "C-1") == 0);
}

int main() {
    static_assert(sizeof(arp::State) <= player::state_bytes, "State fits the SDK's area");
    euclid();
    parameters_clamp();
    first_note_is_prompt();
    chord_sets_the_first_note();
    step_sequenced_chord();
    rates_and_event_drains();
    gates();
    styles();
    chord_of_eight();
    chance();
    cleanup();
    parked_tones_never_sound();
    live_edits();
    screen();
    std::puts("euclid_arp: all checks passed");
    return 0;
}
