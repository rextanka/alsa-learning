#include <gtest/gtest.h>
#include "CInterface.h"
#include <vector>

class BridgeTest : public ::testing::Test {
protected:
    const unsigned int sample_rate = 44100;
    const size_t block_size = 128;
};

TEST_F(BridgeTest, LifecycleAndProcess) {
    // 1. Create Engine
    EngineHandle engine = engine_create(sample_rate);
    ASSERT_NE(engine, nullptr);

    // 2. Setup output buffer (stereo)
    std::vector<float> buffer(block_size * 2, 0.0f);

    // 3. Process before note - should be silent
    int result = engine_process(engine, buffer.data(), block_size);
    EXPECT_EQ(result, 0);
    
    float max_silent = 0.0f;
    for (float sample : buffer) {
        if (std::abs(sample) > max_silent) max_silent = std::abs(sample);
    }
    EXPECT_NEAR(max_silent, 0.0f, 1e-6f);

    // 4. Trigger a note
    engine_note_on(engine, 60, 0.8f); // C4

    // 5. Process again - should have audio
    result = engine_process(engine, buffer.data(), block_size);
    EXPECT_EQ(result, 0);

    float max_audio = 0.0f;
    for (float sample : buffer) {
        if (std::abs(sample) > max_audio) max_audio = std::abs(sample);
    }
    EXPECT_GT(max_audio, 0.0f);

    // 6. Cleanup
    engine_destroy(engine);
}

TEST_F(BridgeTest, NoteOff) {
    EngineHandle engine = engine_create(sample_rate);
    std::vector<float> buffer(block_size * 2, 0.0f);

    engine_note_on(engine, 60, 0.8f);
    engine_note_off(engine, 60);

    // Process some blocks to allow for release (if any)
    // For now, we just check that it doesn't crash
    int result = engine_process(engine, buffer.data(), block_size);
    EXPECT_EQ(result, 0);

    engine_destroy(engine);
}
