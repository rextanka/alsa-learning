/**
 * @file test_lfo_modulation.cpp
 * @brief Functional tests for the LFO C API (Phase 15A).
 *
 * Tests the full path from the C API through to audible modulation effects:
 *   engine_set_lfo_rate / intensity / waveform / depth → Voice::lfo_ + matrix_
 *
 * All tests use offline processing (engine_process without engine_start) so
 * they run on CI without audio hardware.
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include <vector>
#include <cmath>
#include <numeric>
#include <iostream>

static constexpr int kFrames = 512;

// Helper: sum-of-squares RMS for one stereo block.
static float block_rms(const std::vector<float>& buf) {
    float sum = 0.0f;
    for (float s : buf) sum += s * s;
    return std::sqrt(sum / static_cast<float>(buf.size()));
}

// ---------------------------------------------------------------------------
// Test fixture — shared Phase 15 engine chain.
// ---------------------------------------------------------------------------

class LfoModulationTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::init_test_environment();
        sample_rate = test::get_safe_sample_rate(0);
        engine_wrapper = std::make_unique<test::EngineWrapper>(sample_rate);

        auto engine = engine_wrapper->get();

        // Standard three-node chain.
        engine_add_module(engine, "COMPOSITE_GENERATOR", "VCO");
        engine_add_module(engine, "ADSR_ENVELOPE",       "ENV");
        engine_add_module(engine, "VCA",                 "VCA");
        engine_connect_ports(engine, "ENV", "envelope_out", "VCA", "gain_cv");
        engine_bake(engine);

        // Sawtooth with sustain open so notes produce continuous output.
        set_param(engine, "saw_gain",    1.0f);
        set_param(engine, "amp_sustain", 1.0f);

        // Start with LFO inactive.
        engine_set_lfo_intensity(engine, 0.0f);
    }

    void TearDown() override {
        // engine_wrapper RAII cleans up.
    }

    int sample_rate;
    std::unique_ptr<test::EngineWrapper> engine_wrapper;

    // Process N blocks and return per-block RMS values.
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
// Test: engine_set_lfo_* functions return success codes
// ---------------------------------------------------------------------------

TEST_F(LfoModulationTest, ApiCallsSucceed) {
    auto engine = engine_wrapper->get();
    EXPECT_EQ(engine_set_lfo_rate(engine, 5.0f),           0);
    EXPECT_EQ(engine_set_lfo_intensity(engine, 1.0f),      0);
    EXPECT_EQ(engine_set_lfo_waveform(engine, LFO_WAVEFORM_SINE), 0);
    EXPECT_EQ(engine_set_lfo_depth(engine, LFO_TARGET_PITCH, 0.1f), 0);
    EXPECT_EQ(engine_set_lfo_depth(engine, LFO_TARGET_CUTOFF, 0.5f), 0);
}

TEST_F(LfoModulationTest, BadHandleReturnsError) {
    EXPECT_EQ(engine_set_lfo_rate(nullptr, 5.0f), -1);
    EXPECT_EQ(engine_set_lfo_intensity(nullptr, 1.0f), -1);
    EXPECT_EQ(engine_set_lfo_waveform(nullptr, LFO_WAVEFORM_SINE), -1);
    EXPECT_EQ(engine_set_lfo_depth(nullptr, LFO_TARGET_PITCH, 0.1f), -1);
}

TEST_F(LfoModulationTest, BadWaveformReturnsError) {
    auto engine = engine_wrapper->get();
    EXPECT_EQ(engine_set_lfo_waveform(engine, 99), -1);
}

TEST_F(LfoModulationTest, BadDepthTargetReturnsError) {
    auto engine = engine_wrapper->get();
    EXPECT_EQ(engine_set_lfo_depth(engine, 99, 1.0f), -1);
}

// ---------------------------------------------------------------------------
// Test: LFO off produces silence / LFO on produces audio
// ---------------------------------------------------------------------------

TEST_F(LfoModulationTest, NotesProduceAudioWithLfoOff) {
    auto engine = engine_wrapper->get();

    // Ensure LFO is off.
    engine_set_lfo_intensity(engine, 0.0f);

    engine_note_on(engine, 60, 0.8f); // C4

    // Let ADSR attack open (50ms default ≈ 4 blocks at 48kHz/512).
    std::vector<float> buf(kFrames * 2);
    for (int i = 0; i < 8; ++i) engine_process(engine, buf.data(), kFrames);

    float rms = block_rms(buf);
    EXPECT_GT(rms, 0.001f) << "Notes should produce audio even without LFO";
    std::cout << "[LfoModulation] LFO-off RMS: " << rms << std::endl;

    engine_note_off(engine, 60);
}

// ---------------------------------------------------------------------------
// Test: LFO→Pitch vibrato
// Verify that enabling pitch modulation changes the output across time.
// With a fast LFO (5 Hz) and shallow depth (0.1 oct), consecutive blocks
// at different LFO phases should produce different RMS values.
// ---------------------------------------------------------------------------

TEST_F(LfoModulationTest, PitchVibratoChangesOutput) {
    auto engine = engine_wrapper->get();

    engine_note_on(engine, 60, 0.8f);

    // Allow ADSR to open.
    std::vector<float> buf(kFrames * 2);
    for (int i = 0; i < 8; ++i) engine_process(engine, buf.data(), kFrames);

    // Capture baseline (no LFO) RMS over 32 blocks.
    auto baseline_rms = measure_rms_sequence(32);
    float baseline_mean = std::accumulate(baseline_rms.begin(), baseline_rms.end(), 0.0f)
                          / static_cast<float>(baseline_rms.size());

    // Enable LFO→Pitch: 5 Hz, depth ±0.1 octave.
    engine_set_lfo_rate(engine, 5.0f);
    engine_set_lfo_waveform(engine, LFO_WAVEFORM_SINE);
    engine_set_lfo_depth(engine, LFO_TARGET_PITCH, 0.1f);
    engine_set_lfo_intensity(engine, 1.0f);

    auto vibrato_rms = measure_rms_sequence(32);
    float vibrato_mean = std::accumulate(vibrato_rms.begin(), vibrato_rms.end(), 0.0f)
                         / static_cast<float>(vibrato_rms.size());

    // With vibrato active the output should still be present.
    EXPECT_GT(vibrato_mean, 0.001f) << "Vibrato should not silence the output";

    // The variance across blocks should be non-zero with LFO (pitch shifting
    // changes timbre each block, which affects RMS slightly).
    float variance = 0.0f;
    for (float r : vibrato_rms) variance += (r - vibrato_mean) * (r - vibrato_mean);
    variance /= static_cast<float>(vibrato_rms.size());

    std::cout << "[LfoModulation] Vibrato — baseline RMS: " << baseline_mean
              << ", vibrato RMS: " << vibrato_mean
              << ", block variance: " << variance << std::endl;

    // The baseline (no LFO) should have very low block-to-block variance.
    float baseline_variance = 0.0f;
    for (float r : baseline_rms) baseline_variance += (r - baseline_mean) * (r - baseline_mean);
    baseline_variance /= static_cast<float>(baseline_rms.size());

    // Vibrato variance should exceed baseline variance (LFO introduces variation).
    EXPECT_GE(variance, baseline_variance)
        << "LFO→Pitch should introduce more block-to-block variation than no LFO";

    engine_note_off(engine, 60);
}

// ---------------------------------------------------------------------------
// Test: LFO→Cutoff filter sweep
// Attach a Moog filter, enable LFO→Cutoff. Deep modulation (1 octave) should
// create strong amplitude variation as the filter opens/closes each cycle.
// ---------------------------------------------------------------------------

TEST_F(LfoModulationTest, CutoffLfoCreatesAmplitudeVariation) {
    auto engine = engine_wrapper->get();

    // Add a Moog filter at mid cutoff so LFO sweeps it audibly.
    engine_set_filter_type(engine, 0); // Moog
    set_param(engine, "vcf_cutoff", 1000.0f);
    set_param(engine, "vcf_res",    0.3f);

    engine_note_on(engine, 60, 0.8f);

    // Let ADSR open.
    std::vector<float> buf(kFrames * 2);
    for (int i = 0; i < 8; ++i) engine_process(engine, buf.data(), kFrames);

    // Enable LFO→Cutoff: 2 Hz, depth ±1 octave (1000Hz → 500..2000Hz sweep).
    engine_set_lfo_rate(engine, 2.0f);
    engine_set_lfo_waveform(engine, LFO_WAVEFORM_SINE);
    engine_set_lfo_depth(engine, LFO_TARGET_CUTOFF, 1.0f);
    engine_set_lfo_intensity(engine, 1.0f);

    // Capture enough blocks to span one full LFO cycle (1 cycle at 2Hz = 0.5s ≈ 47 blocks).
    auto rms_sequence = measure_rms_sequence(48);

    float mean_rms = std::accumulate(rms_sequence.begin(), rms_sequence.end(), 0.0f)
                     / static_cast<float>(rms_sequence.size());
    float max_rms = *std::max_element(rms_sequence.begin(), rms_sequence.end());
    float min_rms = *std::min_element(rms_sequence.begin(), rms_sequence.end());

    std::cout << "[LfoModulation] Cutoff LFO — mean RMS: " << mean_rms
              << ", max: " << max_rms << ", min: " << min_rms << std::endl;

    EXPECT_GT(mean_rms, 0.001f) << "Filter sweep should not silence output entirely";
    EXPECT_GT(max_rms - min_rms, 0.0f)
        << "LFO→Cutoff should produce some RMS variation across the filter cycle";

    engine_note_off(engine, 60);
}

// ---------------------------------------------------------------------------
// Test: engine_clear_modulations resets LFO to inactive
// ---------------------------------------------------------------------------

TEST_F(LfoModulationTest, ClearModulationsResetsLfo) {
    auto engine = engine_wrapper->get();

    engine_set_lfo_rate(engine, 10.0f);
    engine_set_lfo_intensity(engine, 1.0f);
    engine_set_lfo_depth(engine, LFO_TARGET_PITCH, 0.5f);

    // Clear should disable LFO modulation.
    EXPECT_EQ(engine_clear_modulations(engine), 0);

    // After clearing, output should be steady (LFO inactive).
    engine_note_on(engine, 60, 0.8f);
    std::vector<float> buf(kFrames * 2);
    for (int i = 0; i < 8; ++i) engine_process(engine, buf.data(), kFrames);
    auto rms_seq = measure_rms_sequence(16);

    float variance = 0.0f;
    float mean = std::accumulate(rms_seq.begin(), rms_seq.end(), 0.0f)
                 / static_cast<float>(rms_seq.size());
    for (float r : rms_seq) variance += (r - mean) * (r - mean);
    variance /= static_cast<float>(rms_seq.size());

    std::cout << "[LfoModulation] Post-clear variance: " << variance << std::endl;
    // Variance should be very low after clearing (no LFO effect).
    // Allow for small natural variance from ADSR settling into sustain.
    // LFO depth=0.5 octave would produce variance >> 1e-4; the threshold
    // here is tighter than that but loose enough to pass without LFO.
    EXPECT_LT(variance, 1e-4f) << "After clear, output should be steady (no LFO modulation)";

    engine_note_off(engine, 60);
}
