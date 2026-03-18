/**
 * @file patch_sequence_test.cpp
 * @brief Functional demonstration: four reference patches each performing an 8-bar phrase.
 *
 * Sections:
 *   1. SH-101 Bass  — "I Feel Love" style sequencer ostinato (126 BPM, F minor)
 *   2. TB-303 Bass  — Acid bassline with live filter sweep (135 BPM, A minor,
 *                     Diode Ladder — closest approximation to the 303's transistor ladder)
 *   3. Juno Pad     — Enya-style legato melody with Chorus Mode II (90 BPM, D major)
 *   4. Drawbar Organ — Modal chorale in D Dorian (100 BPM)
 *
 * Each section loads its patch via engine_load_patch, starts the engine, plays the
 * phrase, then stops.  engine_start / engine_stop ensure audio output is heard live.
 */

#include "../TestHelper.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Sequencer primitives
// ---------------------------------------------------------------------------

struct NoteEvent {
    int midi;    // MIDI note number; -1 = rest
    int on_ms;   // note-on duration (ms)
    int off_ms;  // silence after note-off before next event (ms)
};

struct TwoVoiceEvent {
    int soprano;  // upper voice MIDI note; -1 = rest
    int bass;     // lower voice MIDI note; -1 = rest
    int on_ms;
    int off_ms;
};

static void play_sequence(EngineHandle engine,
                          const std::vector<NoteEvent>& seq,
                          int repeats = 1)
{
    for (int r = 0; r < repeats; ++r) {
        for (const auto& ev : seq) {
            if (ev.midi >= 0) engine_note_on(engine, ev.midi, 0.85f);
            std::this_thread::sleep_for(std::chrono::milliseconds(ev.on_ms));
            if (ev.midi >= 0) engine_note_off(engine, ev.midi);
            if (ev.off_ms > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(ev.off_ms));
        }
    }
}

static void play_two_voice(EngineHandle engine,
                           const std::vector<TwoVoiceEvent>& seq)
{
    for (const auto& ev : seq) {
        if (ev.soprano >= 0) engine_note_on(engine, ev.soprano, 0.80f);
        if (ev.bass    >= 0) engine_note_on(engine, ev.bass,    0.70f);
        std::this_thread::sleep_for(std::chrono::milliseconds(ev.on_ms));
        if (ev.soprano >= 0) engine_note_off(engine, ev.soprano);
        if (ev.bass    >= 0) engine_note_off(engine, ev.bass);
        if (ev.off_ms > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(ev.off_ms));
    }
}

// ---------------------------------------------------------------------------
// Section 1 — SH-101  "I Feel Love" ostinato  (126 BPM, F minor)
// ---------------------------------------------------------------------------
// 4/4 at 126 BPM: 8th note = 60000 / (126×2) ≈ 238 ms
// 8 8th notes per bar.  Two-bar phrase × 4 repeats = 8 bars.
// Notes: F2=41  Eb2=39  Ab2=44  C2=36  Db2=37
// ---------------------------------------------------------------------------
static void run_sh101(int sample_rate, const std::string& patch_dir)
{
    std::cout << "\n=== Section 1: SH-101  \"I Feel Love\" ostinato  (126 BPM, F minor) ===\n";

    test::EngineWrapper engine(sample_rate);
    const std::string patch = patch_dir + "/sh_bass.json";
    if (engine_load_patch(engine.get(), patch.c_str()) != 0) {
        std::cerr << "[SH101] Failed to load patch.\n"; return;
    }
    engine_start(engine.get());

    // 8th note grid: on=185 ms, gap=53 ms (slightly staccato for that sequencer snap)
    constexpr int ON = 185, GAP = 53;

    // Two-bar phrase (16 8th notes):
    //   Bar 1: F2  F2  Eb2  F2  |  Ab2  F2  Eb2  C2
    //   Bar 2: F2  F2  Eb2  F2  |  Ab2  Eb2  Db2  F2
    const std::vector<NoteEvent> phrase = {
        {41, ON, GAP}, {41, ON, GAP}, {39, ON, GAP}, {41, ON, GAP},
        {44, ON, GAP}, {41, ON, GAP}, {39, ON, GAP}, {36, ON, GAP},
        {41, ON, GAP}, {41, ON, GAP}, {39, ON, GAP}, {41, ON, GAP},
        {44, ON, GAP}, {39, ON, GAP}, {37, ON, GAP}, {41, ON, GAP},
    };

    play_sequence(engine.get(), phrase, 4);  // 4 × 2 bars = 8 bars

    engine_stop(engine.get());
    std::cout << "[SH101] Done.\n";
}

// ---------------------------------------------------------------------------
// Section 2 — TB-303  acid bassline with live filter sweep  (135 BPM, A minor)
// ---------------------------------------------------------------------------
// 4/4 at 135 BPM: 16th note = 60000 / (135×4) ≈ 111 ms
// 16 16th-notes per bar.  Filter sweeps bar-by-bar over 8 bars.
// Diode Ladder (type 1) — closest available approximation to the 303's
// three-pole transistor ladder filter topology.
// Notes: A1=33  A2=45  D2=38  E2=40  G1=31  C2=36
// ---------------------------------------------------------------------------
static void run_tb303(int sample_rate, const std::string& patch_dir)
{
    std::cout << "\n=== Section 2: TB-303  acid bassline with filter sweep  (135 BPM, A minor) ===\n";

    test::EngineWrapper engine(sample_rate);
    const std::string patch = patch_dir + "/tb_bass.json";
    if (engine_load_patch(engine.get(), patch.c_str()) != 0) {
        std::cerr << "[TB303] Failed to load patch.\n"; return;
    }

    // Filter type is specified in tb_bass.json (DIODE_FILTER chain node).
    engine_start(engine.get());

    // 16th note grid: on=78 ms, gap=33 ms (very staccato, classic 303 pluck)
    constexpr int ON = 78, GAP = 33;

    // One-bar phrase (16 16th-notes):
    //   low root → octave up → 4th → 5th pattern with chromatic passing tones
    const std::vector<NoteEvent> bar = {
        {33, ON, GAP}, {33, ON, GAP}, {45, ON, GAP}, {38, ON, GAP},
        {45, ON, GAP}, {33, ON, GAP}, {40, ON, GAP}, {38, ON, GAP},
        {33, ON, GAP}, {45, ON, GAP}, {33, ON, GAP}, {40, ON, GAP},
        {38, ON, GAP}, {33, ON, GAP}, {36, ON, GAP}, {40, ON, GAP},
    };

    // 8 bars: cutoff sweeps 300 → 3000 Hz, resonance builds 0.60 → 0.88
    for (int b = 0; b < 8; ++b) {
        const float t      = static_cast<float>(b) / 7.0f;
        const float cutoff = 300.0f  + t * 2700.0f;
        const float res    = 0.60f   + t * 0.28f;
        set_param(engine.get(), "vcf_cutoff", cutoff);
        set_param(engine.get(), "vcf_res",    res);
        std::cout << "[TB303] Bar " << (b + 1)
                  << "  cutoff=" << static_cast<int>(cutoff)
                  << " Hz  res=" << res << "\n";
        play_sequence(engine.get(), bar, 1);
    }

    engine_stop(engine.get());
    std::cout << "[TB303] Done.\n";
}

// ---------------------------------------------------------------------------
// Section 3 — Juno Pad  Enya-style legato melody  (90 BPM, D major, Chorus II)
// ---------------------------------------------------------------------------
// 4/4 at 90 BPM: quarter note = 60000 / 90 ≈ 667 ms
// 4 quarter notes per bar × 8 bars = 32 notes.
// Chorus Mode II for the characteristic Juno shimmer.
// Notes (D major scale): D4=62  E4=64  F#4=66  G4=67  A4=69  B4=71  C#5=73  D5=74
// ---------------------------------------------------------------------------
static void run_juno(int sample_rate, const std::string& patch_dir)
{
    std::cout << "\n=== Section 3: Juno Pad  Enya-style melody  (90 BPM, D major, Chorus II) ===\n";

    test::EngineWrapper engine(sample_rate);
    const std::string patch = patch_dir + "/juno_pad.json";
    if (engine_load_patch(engine.get(), patch.c_str()) != 0) {
        std::cerr << "[Juno] Failed to load patch.\n"; return;
    }
    engine_set_chorus_enabled(engine.get(), 1);
    engine_set_chorus_mode(engine.get(), 2);
    engine_start(engine.get());

    // Quarter note: on=620 ms, gap=47 ms (long legato, slight breath between notes)
    constexpr int ON = 620, GAP = 47;

    // 8-bar descending/ascending melody in D major:
    //   Bars 1–2: D5  B4  A4  G4  |  F#4  E4  D4  D5
    //   Bars 3–4: E4  F#4  G4  A4  |  B4  A4  G4  F#4
    //   Bars 5–6: D5  B4  A4  G4  |  F#4  E4  D4  D5
    //   Bars 7–8: D4  E4  F#4  A4  |  D5  A4  F#4  D4
    const std::vector<NoteEvent> melody = {
        {74, ON, GAP}, {71, ON, GAP}, {69, ON, GAP}, {67, ON, GAP},
        {66, ON, GAP}, {64, ON, GAP}, {62, ON, GAP}, {74, ON, GAP},
        {64, ON, GAP}, {66, ON, GAP}, {67, ON, GAP}, {69, ON, GAP},
        {71, ON, GAP}, {69, ON, GAP}, {67, ON, GAP}, {66, ON, GAP},
        {74, ON, GAP}, {71, ON, GAP}, {69, ON, GAP}, {67, ON, GAP},
        {66, ON, GAP}, {64, ON, GAP}, {62, ON, GAP}, {74, ON, GAP},
        {62, ON, GAP}, {64, ON, GAP}, {66, ON, GAP}, {69, ON, GAP},
        {74, ON, GAP}, {69, ON, GAP}, {66, ON, GAP}, {62, ON, GAP},
    };

    play_sequence(engine.get(), melody, 1);

    engine_stop(engine.get());
    std::cout << "[Juno] Done.\n";
}

// ---------------------------------------------------------------------------
// Section 4 — Drawbar Organ  D Dorian modal chorale  (100 BPM)
// ---------------------------------------------------------------------------
// 4/4 at 100 BPM: quarter note = 60000 / 100 = 600 ms
// 4 quarter notes per bar × 8 bars = 32 notes.
// D Dorian: D  E  F  G  A  B  C  D  (natural minor with raised 6th)
// Notes: D4=62  E4=64  F4=65  G4=67  A4=69  B4=71  C5=72  D5=74  Bb3=58
// Deliberately different melody from BachOrganTest (which uses BWV 578).
// ---------------------------------------------------------------------------
static void run_organ(int sample_rate, const std::string& patch_dir)
{
    std::cout << "\n=== Section 4: Drawbar Organ  D Dorian chorale  (100 BPM) ===\n";

    test::EngineWrapper engine(sample_rate);
    const std::string patch = patch_dir + "/organ_drawbar.json";
    if (engine_load_patch(engine.get(), patch.c_str()) != 0) {
        std::cerr << "[Organ] Failed to load patch.\n"; return;
    }
    engine_start(engine.get());

    // Quarter note: on=580 ms, gap=20 ms (organ-style legato — minimal gap)
    constexpr int ON = 580, GAP = 20;

    // Two-voice counterpoint in D Dorian (soprano + bass, contrary motion,
    // all intervals consonant: P8, P5, M/m3, M/m6 and compound equivalents).
    //
    // Soprano (upper): D5 D5 A4 F4 | E4 F4 G4 A4 | B4 A4 G4 F4 | E4 E4 D4 D4
    //                  D4 E4 F4 G4 | A4 C5 B4 A4 | G4 A4 B4 C5 | D5 D5 D5 D5
    // Bass    (lower): D3 F3 A3 A2 | A2 D3 E3 F3 | G3 A3 C3 D3 | C3 G3 F3 D3
    //                  D3 C3 A2 G2 | D3 E3 G3 F3 | E3 C3 D3 A3 | D3 G3 F3 D3
    const std::vector<TwoVoiceEvent> chorale = {
        // Bar 1
        {74, 50, ON, GAP}, {74, 53, ON, GAP}, {69, 57, ON, GAP}, {65, 45, ON, GAP},
        // Bar 2
        {64, 45, ON, GAP}, {65, 50, ON, GAP}, {67, 52, ON, GAP}, {69, 53, ON, GAP},
        // Bar 3
        {71, 55, ON, GAP}, {69, 57, ON, GAP}, {67, 48, ON, GAP}, {65, 50, ON, GAP},
        // Bar 4
        {64, 48, ON, GAP}, {64, 55, ON, GAP}, {62, 53, ON, GAP}, {62, 50, ON, GAP},
        // Bar 5
        {62, 50, ON, GAP}, {64, 48, ON, GAP}, {65, 45, ON, GAP}, {67, 43, ON, GAP},
        // Bar 6
        {69, 50, ON, GAP}, {72, 52, ON, GAP}, {71, 55, ON, GAP}, {69, 53, ON, GAP},
        // Bar 7
        {67, 52, ON, GAP}, {69, 48, ON, GAP}, {71, 50, ON, GAP}, {72, 57, ON, GAP},
        // Bar 8 — tonic resolution
        {74, 50, ON, GAP}, {74, 55, ON, GAP}, {74, 53, ON, GAP}, {74, 50, ON, GAP},
    };

    play_two_voice(engine.get(), chorale);

    engine_stop(engine.get());
    std::cout << "[Organ] Done.\n";
}

// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    // -p <dir>  Override the patch directory (default: "patches").
    // Useful when running from build/bin: ./patch_sequence_test -p ../../patches
    std::string patch_dir = "patches";
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "-p") {
            patch_dir = argv[i + 1];
            ++i;
        }
    }

    test::init_test_environment();
    const int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Reference Patch Musical Sequences",
        "Loads each of the four reference patches and performs an 8-bar musical phrase.",
        "engine_load_patch -> SH-101 / TB-303 / Juno Pad / Drawbar Organ -> Output",
        "Four distinct timbres, each audible and rhythmically coherent for 8 bars.",
        sample_rate
    );

    std::cout << "[patches] " << patch_dir << "\n";

    run_sh101(sample_rate, patch_dir);
    run_tb303(sample_rate, patch_dir);
    run_juno(sample_rate, patch_dir);
    run_organ(sample_rate, patch_dir);

    std::cout << "\n=== All patch sequences complete. Engine destroyed via RAII. ===\n";
    return 0;
}
