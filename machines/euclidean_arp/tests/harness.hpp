// A host model of the SDK's player adapter and stock's chord records, following
// player_object.cpp.in, player_plans.hpp and player_activity.hpp in the SDK.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "player.cpp"

#define require(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)

using opxy::i32;
using opxy::u32;
namespace arp = euclid_arp;
namespace player = opxy::player;

struct Event {
    u32 time, pitch, velocity;
    bool on;
};

struct Host {
    struct Sound { i32 delay; u32 pitch, velocity; };
    struct Record { Sound tones[8]; u32 count, root; };

    arp::State state;
    player::Controls controls;
    Record records[8];
    u32 held, now, stale_updates, passed_through, largest_drain;
    int level[128];
    std::vector<Event> events;

    Host() : state(), controls(), records(), held(0), now(0), stale_updates(0), passed_through(0),
             largest_drain(0), level() {}

    // Stores a choice as the SDK does: the centre of its bucket.
    void set(u32 control, u32 index, u32 choices) {
        const u32 width = 0x7fffffffu / choices;
        controls.value[control] = static_cast<i32>(index * width + width / 2);
    }
    void configure(u32 rate, u32 style, u32 octaves, u32 gate_percent, u32 steps, u32 hits, u32 rotate, u32 chance,
                   bool keep_first = false) {
        set(0, rate, 4); set(1, style, arp::kStyles); set(2, octaves - 1, 4); set(3, (gate_percent - 10) / 5, 19);
        set(4, steps - 1, 16); set(5, hits, 17); set(6, rotate, 16); set(7, keep_first ? 40 - chance / 5 : chance / 5, 41);
    }

    Record* find(u32 root) {
        for (u32 i = 0; i < held; ++i)
            if (records[i].count && records[i].root == root) return &records[i];
        return nullptr;
    }
    void note_off(Record& record) {
        for (u32 i = 0; i < record.count; ++i)
            if (record.tones[i].delay < 0) {
                --level[record.tones[i].pitch];
                events.push_back(Event{now, record.tones[i].pitch, 0, false});
            }
        record.count = 0;
    }
    void stock_release(u32 root) {
        for (u32 i = 0; i < held; ++i)
            if (records[i].count && records[i].root == root) note_off(records[i]);
    }
    void store(u32 root, const player::Plan& plan, bool existing_only) {
        require(plan.count <= player::max_tones);
        Record* record = find(root);
        if (existing_only && !record) { ++stale_updates; return; }
        if (record) stock_release(root);
        if (!plan.count) return;
        if (!record) {
            for (u32 i = 0; i < held && !record; ++i)
                if (!records[i].count) record = &records[i];
            if (!record) { require(held < 8); record = &records[held++]; }
        }
        record->root = root;
        record->count = plan.count;
        for (u32 i = 0; i < plan.count; ++i) {
            require(plan.tones[i].pitch <= 127 && plan.tones[i].velocity <= 127);
            record->tones[i] = Sound{plan.tones[i].delay, plan.tones[i].pitch, plan.tones[i].velocity};
        }
    }
    void drain() {
        player::Update change;
        u32 i = 0;
        for (; i < 8 && arp::update(state, controls, change); ++i) {
            require(change.mode == player::Replace);
            require(change.plan.count == 1);  // an empty Replace would end the root for good
            store(change.root, change.plan, true);
        }
        if (i > largest_drain) largest_drain = i;
    }
    void press(u32 note, u32 velocity = 100) {
        player::Plan plan;
        if (!arp::press(state, controls, note, velocity, plan)) { ++passed_through; return; }
        if (plan.count) store(note, plan, false);
        drain();
    }
    void release(u32 note) {
        if (!arp::release(state, note)) stock_release(note);
        drain();
    }
    void step(u32 count = 1) {
        for (u32 n = 0; n < count; ++n) {
            drain();
            for (u32 i = 0; i < held; ++i)
                for (u32 j = 0; j < records[i].count; ++j) {
                    Sound& tone = records[i].tones[j];
                    if (tone.delay == 0) {
                        ++level[tone.pitch];
                        events.push_back(Event{now, tone.pitch, tone.velocity, true});
                    }
                    --tone.delay;
                }
            ++now;
        }
    }
    void all_notes_off() {
        for (u32 i = 0; i < held; ++i) note_off(records[i]);
        held = 0;
        arp::reset(state);
    }
    std::vector<Event> ons() const {
        std::vector<Event> out;
        for (const Event& e : events) if (e.on) out.push_back(e);
        return out;
    }
    std::vector<u32> pitches() const {
        std::vector<u32> out;
        for (const Event& e : events) if (e.on) out.push_back(e.pitch);
        return out;
    }
    int sounding() const {
        int total = 0;
        for (int value : level) { require(value >= 0); total += value; }
        return total;
    }
};
