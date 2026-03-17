/**
 * @file test_lfo_modulation.cpp
 * @brief Functional tests for chain-based LFO modulation (Phase 16).
 *
 * LFO is a first-class chain module wired via named ports:
 *   engine_add_module(h, "LFO", "LFO1")
 *   engine_connect_ports(h, "LFO1", "control_out", "VCO", "pitch_cv")
 *   set_param(h, "rate",      hz)
 *   set_param(h, "intensity", v)
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include <vector>
#include <cmath>
#include <numeric>
#include <iostream>

static constexpr int kFrames = 512;

static float block_rms(const std::vector<float>& buf) {
    float sum = 0.0f;
    for (float s : buf) sum += s * s;
    return std::sqrt(sum / static_cast<float>(buf.size()));
}

// ---------------------------------------------------------------------------
// Fixture — three-node chain (VCO → ENV → VCA) used by most tests.
// ---------------------------------------------------------------------------

class LfoModulationTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
        auto engine = engine_wrapper->get();

        engine_add_module(engine, "COMPOSITE_GENERATOR", "VCO");
        engine_add_module(engine, "ADSR_ENVELOPE",       "ENV");
        engine_add_module(engine, "VCA",                 "VCA");
        engine_connect_ports(engine, "ENV", "envelope_out", "VCA", "gain_cv");
        engine_bake(engine);

        set_param(engine, "saw_gain",    1.0f);
        set_param(engine, "amp_sustain", 1.0f);
    }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    std::vector<float> measure_rms_sequence(int num_blocks) {
        auto engine = engine_wrapper->get();
        std::vector<float> rms_values;
        std::vector<float> buf(kFrames * 2);
        rms_values.reserve(num_blocks);
        for (int i = 0; i < num_blocks; ++i) {
            engine_process(engine, buf.data(), kFrames);
            rms_values.push_back(block_rms(buf));
        }
        return rms_values;
    }
};

// ---------------------------------------------------------------------------
// Test: notes produce audio without LFO in the chain
// ---------------------------------------------------------------------------

TEST_F(LfoModulationTest, NotesProduceAudioWithoutLfo) {
    auto engine = engine_wrapper->get();
    engine_note_on(engine, 60, 0.8f);

    std::vector<float> buf(kFrames * 2);
    for (int i = 0; i < 8; ++i) engine_process(engine, buf.data(), kFrames);

    float rms = block_rms(buf);
    EXPECT_GT(rms, 0.001f) << "Notes should produce audio without LFO";
    std::cout << "[LfoModulation] No-LFO RMS: " << rms << "\n";

    engine_note_off(engine, 60);
}

// ---------------------------------------------------------------------------
// Fixture — chain with LFO wired to VCO pitch_cv.
// Chain order: LFO (PORT_CONTROL) → VCO → ENV → VCA
// ---------------------------------------------------------------------------

class LfoPitchChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);
        auto engine = engine_wrapper->get();

        // LFO first so its ctrl buffer is filled before VCO is processed.
        engine_add_module(engine, "LFO",                 "LFO1");
        engine_add_module(engine, "COMPOSITE_GENERATOR", "VCO");
        engine_add_module(engine, "ADSR_ENVELOPE",       "ENV");
        engine_add_module(engine, "VCA",                 "VCA");
        engine_connect_ports(engine, "LFO1", "control_out", "VCO",  "pitch_cv");
        engine_connect_ports(engine, "ENV",  "envelope_out", "VCA", "gain_cv");
        engine_bake(engine);

        set_param(engine, "saw_gain",    1.0f);
        set_param(engine, "amp_sustain", 1.0f);
    }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    std::vector<float> measure_rms_sequence(int num_blocks) {
        auto engine = engine_wrapper->get();
        std::vector<float> rms_values;
        std::vector<float> buf(kFrames * 2);
        rms_values.reserve(num_blocks);
        for (int i = 0; i < num_blocks; ++i) {
            engine_process(engine, buf.data(), kFrames);
            rms_values.push_back(block_rms(buf));
        }
        return rms_values;
    }
};

// ---------------------------------------------------------------------------
// Test: LFO intensity=0 → steady output (no vibrato)
// ---------------------------------------------------------------------------

TEST_F(LfoPitchChainTest, IntensityZeroProducesSteadyOutput) {
    auto engine = engine_wrapper->get();
    set_param(engine, "rate",      5.0f);
    set_param(engine, "intensity", 0.0f); // LFO silent

    engine_note_on(engine, 60, 0.8f);
    std::vector<float> buf(kFrames * 2);
    for (int i = 0; i < 8; ++i) engine_process(engine, buf.data(), kFrames);

    auto rms_seq = measure_rms_sequence(32);
    float mean = std::accumulate(rms_seq.begin(), rms_seq.end(), 0.0f)
                 / static_cast<float>(rms_seq.size());
    float variance = 0.0f;
    for (float r : rms_seq) variance += (r - mean) * (r - mean);
    variance /= static_cast<float>(rms_seq.size());

    EXPECT_GT(mean, 0.001f) << "Should produce audio with LFO silenced";
    EXPECT_LT(variance, 1e-4f) << "No vibrato: block-to-block variance should be near zero";
    std::cout << "[LfoModulation] intensity=0 variance: " << variance << "\n";

    engine_note_off(engine, 60);
}

// ---------------------------------------------------------------------------
// Test: LFO pitch vibrato causes waveform content to differ between blocks at
// opposite LFO phases (peak+ vs peak-). With a 1-octave swing the sawtooth
// waveform advances at different rates, so sample content diverges.
// ---------------------------------------------------------------------------

TEST_F(LfoPitchChainTest, PitchVibratoChangesWaveformContent) {
    auto engine = engine_wrapper->get();

    engine_note_on(engine, 60, 0.8f);
    std::vector<float> buf(kFrames * 2);

    // Let ADSR open and reach steady state.
    for (int i = 0; i < 8; ++i) engine_process(engine, buf.data(), kFrames);

    // Enable deep vibrato: 5 Hz, full intensity (sawtooth swings ±1 octave).
    set_param(engine, "rate",      5.0f);
    set_param(engine, "intensity", 1.0f);

    // At 48 kHz / 512 frames, one 5 Hz LFO cycle = ~18.75 blocks.
    // Quarter-cycle ≈ 4-5 blocks (LFO near peak positive).
    for (int i = 0; i < 5; ++i) engine_process(engine, buf.data(), kFrames);
    std::vector<float> block_a = buf;

    // Half-cycle later ≈ 9 more blocks (LFO near peak negative).
    for (int i = 0; i < 9; ++i) engine_process(engine, buf.data(), kFrames);
    std::vector<float> block_b = buf;

    // Blocks at opposite LFO phases should contain different waveform content.
    float diff_sq = 0.0f;
    for (size_t i = 0; i < block_a.size(); ++i) {
        float d = block_a[i] - block_b[i];
        diff_sq += d * d;
    }
    float diff_rms = std::sqrt(diff_sq / static_cast<float>(block_a.size()));

    EXPECT_GT(diff_rms, 0.001f) << "Blocks at opposite LFO phases should differ";
    std::cout << "[LfoModulation] Vibrato diff_rms between opposite phases: " << diff_rms << "\n";

    engine_note_off(engine, 60);
}

// NOTE: LFO→cutoff_cv modulation requires the filter to be part of the signal
// chain as a named node. The current architecture uses a chain-external filter_
// member on Voice. A future phase will move the filter into the chain,
// enabling: LFO.control_out → FILTER.cutoff_cv via engine_connect_ports.
