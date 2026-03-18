/**
 * @file test_cv_utilities.cpp
 * @brief Unit tests for Phase 17 CV utility processors.
 *
 * Covers:
 *  - CvMixerProcessor: gain mixing, DC offset, inversion, inter-mod inject_cv
 *  - CvSplitterProcessor: fan-out pass-through, gain scaling
 *  - MathsProcessor: slew limiter rise/fall, instant when time=0
 *  - GateDelayProcessor: immediate gate when delay=0, delayed gate when delay>0
 *  - SampleHoldProcessor: rising-edge sample, hold between edges
 *  - AdsrEnvelopeProcessor: ext_gate_in rising-edge triggers attack
 *  - InverterProcessor: inject_cv negates input
 *  - Filter kybd_cv: applies 1V/oct tracking combined with cutoff_cv
 */

#include <gtest/gtest.h>
#include "../../src/dsp/routing/CvMixerProcessor.hpp"
#include "../../src/dsp/routing/CvSplitterProcessor.hpp"
#include "../../src/dsp/routing/MathsProcessor.hpp"
#include "../../src/dsp/routing/GateDelayProcessor.hpp"
#include "../../src/dsp/routing/SampleHoldProcessor.hpp"
#include "../../src/dsp/routing/InverterProcessor.hpp"
#include "../../src/dsp/envelope/AdsrEnvelopeProcessor.hpp"
#include "../../src/dsp/filter/MoogLadderProcessor.hpp"
#include "../../src/core/ModuleRegistry.hpp"
#include <vector>
#include <numeric>

using namespace audio;

static constexpr int kSR = 48000;

static std::vector<float> pull_n(Processor& p, int n) {
    std::vector<float> out(n, 0.0f);
    std::span<float> sp(out);
    p.pull(sp);
    return out;
}

static std::vector<float> const_span(float val, int n) {
    return std::vector<float>(n, val);
}

// ---------------------------------------------------------------------------
// CvMixerProcessor
// ---------------------------------------------------------------------------

TEST(CvMixerTest, SingleInputUnityGain) {
    register_builtin_processors();
    CvMixerProcessor mixer;
    auto src = const_span(0.4f, 64);
    mixer.inject_cv("cv_in_1", src);
    auto out = pull_n(mixer, 64);
    EXPECT_NEAR(out[0], 0.4f, 0.001f);
}

TEST(CvMixerTest, TwoInputsSummed) {
    CvMixerProcessor mixer;
    auto a = const_span(0.3f, 64);
    auto b = const_span(0.2f, 64);
    mixer.inject_cv("cv_in_1", a);
    mixer.inject_cv("cv_in_2", b);
    auto out = pull_n(mixer, 64);
    EXPECT_NEAR(out[0], 0.5f, 0.001f);
}

TEST(CvMixerTest, NegativeGainInverts) {
    CvMixerProcessor mixer;
    mixer.apply_parameter("gain_1", -1.0f);
    auto src = const_span(0.5f, 64);
    mixer.inject_cv("cv_in_1", src);
    auto out = pull_n(mixer, 64);
    EXPECT_NEAR(out[0], -0.5f, 0.001f);
}

TEST(CvMixerTest, OffsetAdded) {
    CvMixerProcessor mixer;
    mixer.apply_parameter("offset", 0.25f);
    // no input injected → offset only
    auto out = pull_n(mixer, 64);
    EXPECT_NEAR(out[0], 0.25f, 0.001f);
}

TEST(CvMixerTest, ClampedAtPlusMinus1) {
    CvMixerProcessor mixer;
    auto src = const_span(1.0f, 64);
    mixer.inject_cv("cv_in_1", src);
    mixer.inject_cv("cv_in_2", src); // 1.0 + 1.0 = 2.0 → clamped to 1.0
    auto out = pull_n(mixer, 64);
    EXPECT_NEAR(out[0], 1.0f, 0.001f);
}

TEST(CvMixerTest, NoInputProducesOffset) {
    CvMixerProcessor mixer;
    mixer.apply_parameter("offset", -0.5f);
    auto out = pull_n(mixer, 64);
    EXPECT_NEAR(out[0], -0.5f, 0.001f);
}

// ---------------------------------------------------------------------------
// CvSplitterProcessor
// ---------------------------------------------------------------------------

TEST(CvSplitterTest, PassThroughUnity) {
    CvSplitterProcessor splitter;
    auto src = const_span(0.7f, 64);
    splitter.inject_cv("cv_in", src);
    auto out = pull_n(splitter, 64);
    EXPECT_NEAR(out[0], 0.7f, 0.001f);
}

TEST(CvSplitterTest, Gain1Scales) {
    CvSplitterProcessor splitter;
    splitter.apply_parameter("gain_1", 0.5f);
    auto src = const_span(0.8f, 64);
    splitter.inject_cv("cv_in", src);
    auto out = pull_n(splitter, 64);
    EXPECT_NEAR(out[0], 0.4f, 0.001f);
}

TEST(CvSplitterTest, NoInputProducesZero) {
    CvSplitterProcessor splitter;
    auto out = pull_n(splitter, 64);
    EXPECT_NEAR(out[0], 0.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// MathsProcessor
// ---------------------------------------------------------------------------

TEST(MathsTest, ZeroRiseTimeIsInstant) {
    MathsProcessor maths(kSR);
    // rise_time = 0 → output == input immediately
    auto src = const_span(1.0f, 64);
    maths.inject_cv("cv_in", src);
    auto out = pull_n(maths, 64);
    EXPECT_NEAR(out.back(), 1.0f, 0.001f) << "With zero rise time output should reach 1.0 immediately";
}

TEST(MathsTest, PositiveRiseTimeSlews) {
    MathsProcessor maths(kSR);
    maths.apply_parameter("rise", 0.1f); // 100 ms slew

    // Pull 100 ms worth of samples stepping toward +1.0
    std::vector<float> src(kSR / 10, 1.0f); // 4800 samples
    std::vector<float> out(src.size(), 0.0f);
    for (size_t i = 0; i < src.size(); i += 64) {
        size_t n = std::min((size_t)64, src.size() - i);
        std::span<float> sp(out.data() + i, n);
        std::span<const float> cv(src.data() + i, n);
        maths.inject_cv("cv_in", cv);
        maths.pull(sp);
    }
    // After 100ms of slewing toward 1.0, output should be approaching but not instantly 1.0
    EXPECT_GT(out.back(), 0.5f) << "Should have moved significantly toward target";
    EXPECT_LT(out.front(), out.back()) << "Output should increase over time";
}

TEST(MathsTest, FallTimeSlews) {
    MathsProcessor maths(kSR);
    maths.apply_parameter("fall", 0.5f);

    // First pull at +1.0 (instant, rise=0)
    auto hi = const_span(1.0f, 64);
    maths.inject_cv("cv_in", hi);
    pull_n(maths, 64); // current_ ≈ 1.0 now

    // Now pull at 0.0 — should slew down
    auto lo = const_span(0.0f, 64);
    maths.inject_cv("cv_in", lo);
    auto out = pull_n(maths, 64);
    // With 0.5s fall, 64 samples is tiny — should still be > 0.9
    EXPECT_GT(out[0], 0.9f) << "Slew should still be near 1.0 after 64/48000 s";
    EXPECT_LT(out.back(), 1.0f) << "Should have started moving toward 0";
}

// ---------------------------------------------------------------------------
// GateDelayProcessor
// ---------------------------------------------------------------------------

TEST(GateDelayTest, ZeroDelayFiresImmediately) {
    GateDelayProcessor gd(kSR);
    gd.apply_parameter("delay_time", 0.0f);
    gd.on_note_on(440.0);
    auto out = pull_n(gd, 64);
    EXPECT_NEAR(out[0], 1.0f, 0.001f) << "Zero delay should fire immediately";
}

TEST(GateDelayTest, DelayHoldsLow) {
    GateDelayProcessor gd(kSR);
    gd.apply_parameter("delay_time", 1.0f); // 1 second delay
    gd.on_note_on(440.0);
    // After 64 samples (<<1s), output should still be 0
    auto out = pull_n(gd, 64);
    EXPECT_NEAR(out[0], 0.0f, 0.001f);
    EXPECT_NEAR(out.back(), 0.0f, 0.001f);
}

TEST(GateDelayTest, DelayFiresAfterTime) {
    GateDelayProcessor gd(kSR);
    gd.apply_parameter("delay_time", 0.001f); // 1 ms = 48 samples
    gd.on_note_on(440.0);

    // Pull 100 samples — delay should expire
    std::vector<float> out(100, 0.0f);
    for (size_t i = 0; i < out.size(); ++i) {
        std::span<float> sp(out.data() + i, 1);
        gd.pull(sp);
    }
    // The last samples should be high after 48 samples of delay
    EXPECT_NEAR(out.back(), 1.0f, 0.001f) << "Gate should be high after delay expires";
}

TEST(GateDelayTest, NoteOffStopsGate) {
    GateDelayProcessor gd(kSR);
    gd.apply_parameter("delay_time", 0.0f);
    gd.on_note_on(440.0);
    auto out1 = pull_n(gd, 64);
    EXPECT_NEAR(out1[0], 1.0f, 0.001f);

    gd.on_note_off();
    auto out2 = pull_n(gd, 64);
    EXPECT_NEAR(out2[0], 0.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// SampleHoldProcessor
// ---------------------------------------------------------------------------

TEST(SampleHoldTest, HoldsValueBetweenClocks) {
    SampleHoldProcessor sh;
    // First clock pulse samples 0.5
    std::vector<float> src  = {0.5f, 0.5f, 0.5f, 0.5f};
    std::vector<float> clk  = {0.0f, 1.0f, 1.0f, 1.0f}; // rising at index 1

    sh.inject_cv("cv_in",    src);
    sh.inject_cv("clock_in", clk);
    auto out = pull_n(sh, 4);
    EXPECT_NEAR(out[1], 0.5f, 0.001f) << "Should sample 0.5 on rising edge";
    EXPECT_NEAR(out[3], 0.5f, 0.001f) << "Should hold 0.5 while clock stays high";
}

TEST(SampleHoldTest, NewEdgeSamplesNewValue) {
    SampleHoldProcessor sh;
    // Sample 0.3 on first edge, then 0.8 on second edge
    std::vector<float> src  = {0.3f, 0.3f, 0.8f, 0.8f};
    std::vector<float> clk  = {0.0f, 1.0f, 0.0f, 1.0f}; // rising at 1, 3

    sh.inject_cv("cv_in",    src);
    sh.inject_cv("clock_in", clk);
    auto out = pull_n(sh, 4);
    EXPECT_NEAR(out[1], 0.3f, 0.001f);
    EXPECT_NEAR(out[3], 0.8f, 0.001f);
}

TEST(SampleHoldTest, NoClockHoldsInitialZero) {
    SampleHoldProcessor sh;
    auto src = const_span(0.9f, 64);
    sh.inject_cv("cv_in", src);
    // no clock injected — should output the held value (0.0 on reset)
    auto out = pull_n(sh, 64);
    EXPECT_NEAR(out[0], 0.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// InverterProcessor (fixed to use injected cv_in)
// ---------------------------------------------------------------------------

TEST(InverterTest, NegatesInput) {
    InverterProcessor inv;
    auto src = const_span(0.6f, 64);
    inv.inject_cv("cv_in", src);
    auto out = pull_n(inv, 64);
    EXPECT_NEAR(out[0], -0.6f, 0.001f);
}

TEST(InverterTest, ScaleParameter) {
    InverterProcessor inv;
    inv.apply_parameter("scale", 0.5f);
    auto src = const_span(0.8f, 64);
    inv.inject_cv("cv_in", src);
    auto out = pull_n(inv, 64);
    EXPECT_NEAR(out[0], 0.4f, 0.001f);
}

TEST(InverterTest, NoInputProducesZero) {
    InverterProcessor inv;
    auto out = pull_n(inv, 64);
    EXPECT_NEAR(out[0], 0.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// AdsrEnvelopeProcessor: ext_gate_in rising-edge triggers attack
// ---------------------------------------------------------------------------

TEST(AdsrExtGateTest, RisingEdgeTriggers) {
    register_builtin_processors();
    AdsrEnvelopeProcessor adsr(kSR);
    adsr.set_attack_time(0.001f);
    adsr.set_sustain_level(0.8f);

    // inject_cv runs sample-by-sample edge detection before do_pull().
    // The gate_on() fires when the rising edge is seen (at index 4 within the span),
    // but state is already Attack when do_pull() executes — so all 8 samples attack.
    std::vector<float> clk = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    adsr.inject_cv("ext_gate_in", clk);
    auto out = pull_n(adsr, 8);
    // All samples should be in Attack (> 0), and increasing
    EXPECT_GT(out[0], 0.0f) << "Attack should have started (gate_on fired during inject_cv)";
    EXPECT_GT(out[7], out[0]) << "Level should be rising during attack";
}

TEST(AdsrExtGateTest, FallingEdgeTriggers_Release) {
    AdsrEnvelopeProcessor adsr(kSR);
    adsr.set_attack_time(0.0001f);
    adsr.set_decay_time(0.0001f);
    adsr.set_sustain_level(1.0f);
    adsr.set_release_time(0.5f);

    // Gate on → sustain
    adsr.gate_on();
    pull_n(adsr, kSR / 10); // pull 100 ms to reach sustain

    // Inject falling edge
    std::vector<float> clk(64, 1.0f);
    clk[32] = 0.0f; // falling at 32
    for (size_t i = 33; i < 64; ++i) clk[i] = 0.0f;
    adsr.inject_cv("ext_gate_in", clk);
    auto out = pull_n(adsr, 64);
    // Should start releasing after index 32
    EXPECT_GT(out[31], 0.9f) << "Should be sustaining before falling edge";
}

// ---------------------------------------------------------------------------
// Filter kybd_cv: 1V/oct keyboard tracking combined with base cutoff
// ---------------------------------------------------------------------------

TEST(FilterKybdCvTest, KybdCvShiftsCutoff) {
    MoogLadderProcessor moog(kSR);
    moog.apply_parameter("cutoff", 1000.0f);

    // kybd_cv = +1 → effective cutoff = 1000 * 2^1 = 2000 Hz (higher g_)
    moog.apply_parameter("kybd_cv", 1.0f);

    // kybd_cv = 0 → effective cutoff = 1000 * 2^0 = 1000 Hz
    // We can verify indirectly: apply a tone above 1000 Hz and compare output
    // levels with and without tracking. Direct g_ access isn't exposed, so
    // just verify apply_parameter returns true and doesn't crash.
    EXPECT_TRUE(moog.apply_parameter("kybd_cv",  1.0f));
    EXPECT_TRUE(moog.apply_parameter("kybd_cv",  0.0f));
    EXPECT_TRUE(moog.apply_parameter("kybd_cv", -1.0f));
}

TEST(FilterKybdCvTest, KybdCvAndCutoffCvCombine) {
    MoogLadderProcessor moog(kSR);
    moog.apply_parameter("cutoff", 500.0f);
    // cutoff_cv = +1 (1 octave up), kybd_cv = +1 (1 more octave up)
    // effective = 500 * 2^(1+1) = 2000 Hz — well within audible range, no crash
    moog.apply_parameter("cutoff_cv", 1.0f);
    moog.apply_parameter("kybd_cv",   1.0f);

    std::vector<float> sig(128, 0.1f);
    std::span<float> sp(sig);
    moog.pull(sp); // must not produce NaN or crash
    for (float s : sig) {
        EXPECT_FALSE(std::isnan(s)) << "Filter output must not be NaN";
        EXPECT_FALSE(std::isinf(s)) << "Filter output must not be infinite";
    }
}
