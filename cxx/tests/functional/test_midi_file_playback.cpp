/**
 * @file test_midi_file_playback.cpp
 * @brief Functional tests for Phase 22A SMF file playback.
 *
 * Tests generate minimal valid SMF files in /tmp, load them via engine_load_midi,
 * and verify both automated signal checks and audible output.
 *
 * Chain: engine_load_patch -> engine_load_midi -> engine_midi_play -> Output
 * Phase 22A: channel-blind polyphonic. All tracks → single VoiceManager.
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include <vector>
#include <fstream>
#include <cstdio>
#include <cmath>
#include <thread>
#include <chrono>

// ---------------------------------------------------------------------------
// Minimal SMF builder
// ---------------------------------------------------------------------------

namespace smf {

static void be16(std::vector<uint8_t>& v, uint16_t n) {
    v.push_back(uint8_t(n >> 8)); v.push_back(uint8_t(n));
}
static void be32(std::vector<uint8_t>& v, uint32_t n) {
    v.push_back(uint8_t(n>>24)); v.push_back(uint8_t(n>>16));
    v.push_back(uint8_t(n>> 8)); v.push_back(uint8_t(n));
}
static void vlq(std::vector<uint8_t>& v, uint32_t n) {
    if (n < 0x80) { v.push_back(uint8_t(n)); return; }
    std::vector<uint8_t> tmp;
    tmp.push_back(n & 0x7F); n >>= 7;
    while (n) { tmp.push_back((n & 0x7F) | 0x80); n >>= 7; }
    for (auto it = tmp.rbegin(); it != tmp.rend(); ++it) v.push_back(*it);
}
static void eot(std::vector<uint8_t>& v) {
    vlq(v, 0); v.push_back(0xFF); v.push_back(0x2F); v.push_back(0x00);
}
static void tempo_event(std::vector<uint8_t>& v, uint32_t us) {
    vlq(v, 0);
    v.push_back(0xFF); v.push_back(0x51); v.push_back(0x03);
    v.push_back(uint8_t(us>>16)); v.push_back(uint8_t(us>>8)); v.push_back(uint8_t(us));
}
static void note_on (std::vector<uint8_t>& v, uint32_t delta, uint8_t note, uint8_t vel=80) {
    vlq(v, delta); v.push_back(0x90); v.push_back(note); v.push_back(vel);
}
static void note_off(std::vector<uint8_t>& v, uint32_t delta, uint8_t note) {
    vlq(v, delta); v.push_back(0x80); v.push_back(note); v.push_back(0);
}

/** Write an SMF track chunk into @p file. */
static void write_track(std::vector<uint8_t>& file, const std::vector<uint8_t>& body) {
    file.insert(file.end(), {'M','T','r','k'});
    be32(file, uint32_t(body.size()));
    file.insert(file.end(), body.begin(), body.end());
}

/** Assemble a Format 0 SMF with one track and an optional tempo meta-event. */
static std::vector<uint8_t> format0(uint16_t ppq, uint32_t us_per_beat,
                                     const std::vector<uint8_t>& notes) {
    std::vector<uint8_t> track;
    if (us_per_beat) tempo_event(track, us_per_beat);
    track.insert(track.end(), notes.begin(), notes.end());
    eot(track);

    std::vector<uint8_t> file;
    file.insert(file.end(), {'M','T','h','d'});
    be32(file, 6); be16(file, 0); be16(file, 1); be16(file, ppq);
    write_track(file, track);
    return file;
}

/** Assemble a Format 1 SMF with a separate tempo track and one note track. */
static std::vector<uint8_t> format1(uint16_t ppq, uint32_t us_per_beat,
                                     const std::vector<uint8_t>& notes) {
    // Track 0: tempo
    std::vector<uint8_t> t0;
    tempo_event(t0, us_per_beat);
    eot(t0);

    // Track 1: notes
    std::vector<uint8_t> t1 = notes;
    eot(t1);

    std::vector<uint8_t> file;
    file.insert(file.end(), {'M','T','h','d'});
    be32(file, 6); be16(file, 1); be16(file, 2); be16(file, ppq);
    write_track(file, t0);
    write_track(file, t1);
    return file;
}

static std::string save(const std::string& name, const std::vector<uint8_t>& data) {
    std::string path = "/tmp/" + name;
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    return path;
}

} // namespace smf

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class MidiFilePlaybackTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
    }

    EngineHandle engine() { return engine_wrapper->get(); }

    void load_organ_chain() {
        EngineHandle h = engine();
        engine_add_module(h, "DRAWBAR_ORGAN", "ORGAN");
        engine_add_module(h, "ADSR_ENVELOPE", "ENV");
        engine_add_module(h, "VCA",           "VCA");
        engine_connect_ports(h, "ENV", "envelope_out", "VCA", "gain_cv");
        engine_bake(h);
        set_param(h, "drawbar_8",   8.0f);
        set_param(h, "drawbar_4",   6.0f);
        set_param(h, "drawbar_223", 4.0f);
        set_param(h, "amp_attack",  0.005f);
        set_param(h, "amp_sustain", 1.0f);
        set_param(h, "amp_release", 0.2f);
        ASSERT_EQ(engine_start(h), 0);
    }

    void load_sine_chain() {
        EngineHandle h = engine();
        engine_add_module(h, "COMPOSITE_GENERATOR", "VCO");
        engine_add_module(h, "ADSR_ENVELOPE",       "ENV");
        engine_add_module(h, "VCA",                 "VCA");
        engine_connect_ports(h, "ENV", "envelope_out", "VCA", "gain_cv");
        engine_bake(h);
        set_param(h, "sine_gain",   1.0f);
        set_param(h, "amp_sustain", 1.0f);
    }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;
};

// ---------------------------------------------------------------------------
// Test 1: Load a valid SMF — no crash, returns 0
// ---------------------------------------------------------------------------

TEST_F(MidiFilePlaybackTest, LoadValidSmfSucceeds) {
    PRINT_TEST_HEADER(
        "SMF Load — Smoke Test",
        "Verify engine_load_midi parses a minimal valid SMF without error.",
        "SmfParser",
        "engine_load_midi returns 0.",
        sample_rate
    );

    std::vector<uint8_t> notes;
    smf::note_on (notes, 0,   60);
    smf::note_off(notes, 480, 60);
    auto path = smf::save("smoke.mid", smf::format0(480, 500000, notes));
    EXPECT_EQ(engine_load_midi(engine(), path.c_str()), 0);
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// Test 2: Missing file returns -1
// ---------------------------------------------------------------------------

TEST_F(MidiFilePlaybackTest, LoadMissingFileReturnsError) {
    EXPECT_EQ(engine_load_midi(engine(), "/tmp/__no_such_file__.mid"), -1);
}

// ---------------------------------------------------------------------------
// Test 3: Null guard
// ---------------------------------------------------------------------------

TEST_F(MidiFilePlaybackTest, NullHandleReturnsError) {
    EXPECT_EQ(engine_load_midi(nullptr, "/tmp/x.mid"), -1);
    EXPECT_EQ(engine_midi_play(nullptr),   -1);
    EXPECT_EQ(engine_midi_stop(nullptr),   -1);
    EXPECT_EQ(engine_midi_rewind(nullptr), -1);
}

// ---------------------------------------------------------------------------
// Test 4: SMF playback produces non-zero audio (offline engine_process path)
// ---------------------------------------------------------------------------

TEST_F(MidiFilePlaybackTest, PlaybackProducesAudio) {
    PRINT_TEST_HEADER(
        "SMF Playback — Signal Check (automated)",
        "Verify engine_process output is non-silent during SMF playback.",
        "SmfParser -> MidiFilePlayer -> VCA -> engine_process",
        "RMS > 0 across 8 × 512-frame blocks.",
        sample_rate
    );

    // C-major scale: C4-D4-E4-F4-G4-A4-B4-C5, 8th-note pairs at 120 BPM
    std::vector<uint8_t> notes;
    const uint8_t scale[] = {60,62,64,65,67,69,71,72};
    for (uint8_t n : scale) {
        smf::note_on (notes, 0,   n);
        smf::note_off(notes, 240, n);
    }
    auto path = smf::save("cmajor.mid", smf::format0(480, 500000, notes));

    load_sine_chain();
    ASSERT_EQ(engine_load_midi(engine(), path.c_str()), 0);
    engine_midi_play(engine());

    const size_t FRAMES = 512;
    std::vector<float> out(FRAMES * 2, 0.0f);
    float sum_sq = 0.0f;
    for (int b = 0; b < 8; ++b) {
        engine_process(engine(), out.data(), FRAMES);
        for (float s : out) sum_sq += s * s;
    }
    float rms = std::sqrt(sum_sq / float(FRAMES * 2 * 8));
    std::cout << "[MidiPlayback] Scale RMS: " << rms << std::endl;
    EXPECT_GT(rms, 0.001f);

    engine_midi_stop(engine());
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// Test 5: Rewind resets position to 0
// ---------------------------------------------------------------------------

TEST_F(MidiFilePlaybackTest, RewindResetsPosition) {
    PRINT_TEST_HEADER(
        "SMF Rewind",
        "Verify engine_midi_rewind returns playhead to tick 0.",
        "MidiFilePlayer",
        "position_ticks == 0 after stop + rewind.",
        sample_rate
    );

    std::vector<uint8_t> notes;
    smf::note_on (notes, 0,   60);
    smf::note_off(notes, 480, 60);
    auto path = smf::save("rewind.mid", smf::format0(480, 500000, notes));

    load_sine_chain();
    ASSERT_EQ(engine_load_midi(engine(), path.c_str()), 0);
    engine_midi_play(engine());

    std::vector<float> out(512 * 2);
    engine_process(engine(), out.data(), 512); // advance playhead

    engine_midi_stop(engine());
    engine_midi_rewind(engine());

    uint64_t tick = 999;
    ASSERT_EQ(engine_midi_get_position(engine(), &tick), 0);
    EXPECT_EQ(tick, 0u);

    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// Test 6: Format 1 multi-track SMF parses and plays
// ---------------------------------------------------------------------------

TEST_F(MidiFilePlaybackTest, Format1MultiTrackParsesOk) {
    PRINT_TEST_HEADER(
        "SMF Format 1 — Multi-Track Parse",
        "Verify Format 1 (separate tempo + note tracks) loads and produces audio.",
        "SmfParser (Format 1) -> MidiFilePlayer",
        "engine_load_midi returns 0 and RMS > 0.",
        sample_rate
    );

    // Two melodic tracks on different channels — Phase 22A merges both.
    std::vector<uint8_t> notes;
    smf::note_on (notes, 0,   60); // C4 ch1
    smf::note_off(notes, 480, 60);
    smf::note_on (notes, 0,   64); // E4 ch1
    smf::note_off(notes, 480, 64);
    auto path = smf::save("format1.mid", smf::format1(480, 500000, notes));

    load_sine_chain();
    ASSERT_EQ(engine_load_midi(engine(), path.c_str()), 0);
    engine_midi_play(engine());

    std::vector<float> out(512 * 2, 0.0f);
    float sum_sq = 0.0f;
    for (int b = 0; b < 4; ++b) {
        engine_process(engine(), out.data(), 512);
        for (float s : out) sum_sq += s * s;
    }
    float rms = std::sqrt(sum_sq / float(512 * 2 * 4));
    EXPECT_GT(rms, 0.001f);

    engine_midi_stop(engine());
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// Test 7: BWV 578 — Little Fugue in G minor, full 4-voice exposition (AUDIBLE)
//         Encoded as SMF Format 1, 160 BPM, pipe organ patch.
//         Entry order: Soprano (G4), Alto/Answer (D5), Tenor (G3), Bass/Answer (D4)
// ---------------------------------------------------------------------------

TEST_F(MidiFilePlaybackTest, BWV578SubjectAudible) {
    PRINT_TEST_HEADER(
        "SMF Playback — BWV 578 Fugue Exposition (audible)",
        "Load BWV 578 4-voice exposition encoded as SMF and play on pipe organ.",
        "SmfParser (Format 1, 160 BPM) -> MidiFilePlayer -> DrawbarOrgan -> Output",
        "Audible G-minor fugal exposition: soprano, alto, tenor, bass (~30s).",
        sample_rate
    );

    // 160 BPM, PPQ=480: µs/beat=375000
    // quarter=480t=375ms, half~=960t=750ms (912t sounding + 48t gap)
    // on_ticks = sounding ticks; gap_ticks = silence after note_off before next note_on
    struct Note { uint8_t pitch; uint32_t on_ticks; uint32_t gap_ticks; };

    // Subject (tonic, G): intervals 0,+7,+3,+2,0,+3,+2,0,-1,+2,-7 from root
    auto make_subject = [](uint8_t root) -> std::vector<Note> {
        const int8_t  iv[] = { 0, 7, 3, 2, 0, 3, 2,  0, -1,  2, -7};
        const uint32_t on[] = {912,912,912,432,432,432,432,432,432,432,1392};
        const uint32_t gp[] = { 48, 48, 48, 48, 48, 48, 48, 48, 48, 48,  96};
        std::vector<Note> v; v.reserve(11);
        for (int i = 0; i < 11; ++i)
            v.push_back({uint8_t(root + iv[i]), on[i], gp[i]});
        return v;
    };

    // Tonal answer (dominant, D): same shape but last interval -5 (stays in tonic key)
    auto make_answer = [](uint8_t root) -> std::vector<Note> {
        const int8_t  iv[] = { 0, 7, 3, 2, 0, 3, 2,  0, -1,  2, -5};
        const uint32_t on[] = {912,912,912,432,432,432,432,432,432,432,1392};
        const uint32_t gp[] = { 48, 48, 48, 48, 48, 48, 48, 48, 48, 48,  96};
        std::vector<Note> v; v.reserve(11);
        for (int i = 0; i < 11; ++i)
            v.push_back({uint8_t(root + iv[i]), on[i], gp[i]});
        return v;
    };

    // Four entries: Soprano (G4=67), Alto answer (D5=74), Tenor (G3=55), Bass answer (D4=62)
    const std::vector<std::vector<Note>> entries = {
        make_subject(67),  // Entry 1 — Soprano: G4
        make_answer (74),  // Entry 2 — Alto:    D5
        make_subject(55),  // Entry 3 — Tenor:   G3
        make_answer (62),  // Entry 4 — Bass:    D4
    };
    const uint8_t velocities[] = {90, 88, 85, 82};

    // Append one entry to the SMF byte buffer.
    // pre_delta = ticks to wait after the preceding note_off.
    auto append_entry = [&](std::vector<uint8_t>& buf,
                            const std::vector<Note>& entry,
                            uint32_t pre_delta, uint8_t vel) {
        for (size_t i = 0; i < entry.size(); ++i) {
            uint32_t onset = (i == 0) ? pre_delta : entry[i-1].gap_ticks;
            smf::note_on (buf, onset,            entry[i].pitch, vel);
            smf::note_off(buf, entry[i].on_ticks, entry[i].pitch);
        }
    };

    constexpr uint32_t REST = 1920; // 1 bar (4 quarter notes) between entries

    std::vector<uint8_t> notes;
    append_entry(notes, entries[0],    0, velocities[0]);
    append_entry(notes, entries[1], REST, velocities[1]);
    append_entry(notes, entries[2], REST, velocities[2]);
    append_entry(notes, entries[3], REST, velocities[3]);

    // Total: ~36 000 ticks @ 1280 t/s = ~28s; allow 32s for safety
    auto path = smf::save("bwv578.mid", smf::format1(480, 375000, notes));
    load_organ_chain();

    ASSERT_EQ(engine_load_midi(engine(), path.c_str()), 0);
    engine_midi_play(engine());

    std::cout << "[MidiPlayback] Playing BWV 578 4-voice fugal exposition on pipe organ (~30s)…" << std::endl;
    test::wait_while_running(32);

    engine_midi_stop(engine());
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// Test 8: Polyphonic C-major triad — delta=0 simultaneous notes (AUDIBLE)
// ---------------------------------------------------------------------------

TEST_F(MidiFilePlaybackTest, PolyphonicChordAudible) {
    PRINT_TEST_HEADER(
        "SMF Playback — Polyphonic Chord (audible)",
        "Verify delta=0 simultaneous Note On events all sound together.",
        "SmfParser -> MidiFilePlayer (delta=0 chord) -> DrawbarOrgan -> Output",
        "Audible C-major triad (C4 E4 G4) held for ~2s.",
        sample_rate
    );

    // Three notes at delta=0 — all dispatched to the same sampleOffset
    std::vector<uint8_t> notes;
    smf::note_on (notes, 0,   60, 80);
    smf::note_on (notes, 0,   64, 80);
    smf::note_on (notes, 0,   67, 80);
    smf::note_off(notes, 960, 60);
    smf::note_off(notes, 0,   64);
    smf::note_off(notes, 0,   67);

    auto path = smf::save("chord.mid", smf::format0(480, 500000, notes));
    load_organ_chain();

    ASSERT_EQ(engine_load_midi(engine(), path.c_str()), 0);
    engine_midi_play(engine());

    std::cout << "[MidiPlayback] Playing C-major triad for ~2s…" << std::endl;
    test::wait_while_running(3);

    engine_midi_stop(engine());
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// Test 9: BWV 772 — Invention No. 1 in C major (AUDIBLE)
//         Real-world polyphonic SMF downloaded from Mutopia Project (CC0).
//         Format 1, 3 tracks (tempo + 2 voices), PPQ=384, 80 BPM.
//         Voices on channels 1 and 2 — Phase 22A merges both.
// ---------------------------------------------------------------------------

TEST_F(MidiFilePlaybackTest, BWV772InventionAudible) {
    PRINT_TEST_HEADER(
        "SMF Playback — BWV 772 Invention No.1 in C major (audible, real SMF)",
        "Load a real Mutopia CC0 MIDI file with 2 independent polyphonic voices.",
        "SmfParser (Format 1, 80 BPM, 2 tracks) -> MidiFilePlayer -> DrawbarOrgan",
        "Audible two-voice invention in C major for ~30s. Both voices should be distinct.",
        sample_rate
    );

    // File lives in assets/midi/bach/ and is copied to midi/bach/ in the build output.
    const char* midi_path = "midi/bach/bwv772_invention1.mid";
    if (std::ifstream test_open(midi_path); !test_open.good()) {
        GTEST_SKIP() << "midi/bach/bwv772_invention1.mid not found — skipping audible test";
    }

    load_organ_chain();

    ASSERT_EQ(engine_load_midi(engine(), midi_path), 0);
    engine_midi_play(engine());

    // Full piece is ~66s at 80 BPM.
    std::cout << "[MidiPlayback] Playing BWV 772 Invention No.1 in C major (~66s)…" << std::endl;
    std::cout << "[MidiPlayback] Source: Mutopia Project, CC0 (public domain)" << std::endl;
    test::wait_while_running(70);

    engine_midi_stop(engine());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
