/**
 * @file test_juno_chorus.cpp
 * @brief Functional verification of Juno Chorus stereo separation via Bridge API.
 *
 * Chain: COMPOSITE_GENERATOR -> ADSR_ENVELOPE -> VCA (Phase 15)
 * The Juno Chorus is applied at the engine output level via engine_set_chorus_enabled.
 */

#include <gtest/gtest.h>
#include "../TestHelper.hpp"
#include <vector>
#include <cmath>

TEST(JunoChorusTest, StereoSeparation) {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Juno Chorus Stereo Separation",
        "Verifies that Juno Chorus creates stereo separation from mono input.",
        "COMPOSITE_GENERATOR -> ADSR_ENVELOPE -> VCA -> JunoChorus -> Output",
        "Significant difference between Left and Right channels after processing.",
        sample_rate
    );

    test::EngineWrapper engine(sample_rate);

    // Phase 15 chain
    engine_add_module(engine.get(), "COMPOSITE_GENERATOR", "VCO");
    engine_add_module(engine.get(), "ADSR_ENVELOPE",       "ENV");
    engine_add_module(engine.get(), "VCA",                 "VCA");
    engine_connect_ports(engine.get(), "ENV", "envelope_out", "VCA", "gain_cv");
    engine_bake(engine.get());

    set_param(engine.get(), "sine_gain",   1.0f);
    set_param(engine.get(), "amp_sustain", 1.0f);

    engine_note_on(engine.get(), 60, 0.8f);

    engine_set_chorus_enabled(engine.get(), 1);
    engine_set_chorus_mode(engine.get(), 1); // Mode I

    const size_t frames = 512;
    std::vector<float> output(frames * 2);

    // Process enough blocks for the chorus LFO to create separation
    for (int i = 0; i < 10; ++i)
        engine_process(engine.get(), output.data(), frames);

    bool different = false;
    for (size_t i = 0; i < frames; ++i) {
        if (std::abs(output[i * 2] - output[i * 2 + 1]) > 0.0001f) {
            different = true;
            break;
        }
    }

    std::cout << "[JunoChorus] Stereo separation: " << (different ? "YES" : "NO") << std::endl;
    EXPECT_TRUE(different);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
