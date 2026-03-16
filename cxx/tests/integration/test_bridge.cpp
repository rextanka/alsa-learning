#include <gtest/gtest.h>
#include "CInterface.h"
#include <vector>
#include <cstring>
#include <fstream>
#include <filesystem>

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

// --- Phase 15: Module Registry and Chain Construction via C API ---

TEST_F(BridgeTest, ModuleRegistryReportsBuiltinTypes) {
    EngineHandle engine = engine_create(sample_rate);
    ASSERT_NE(engine, nullptr);

    int count = engine_get_module_count(engine);
    EXPECT_GE(count, 9); // at least 9 built-in types (includes DRAWBAR_ORGAN)

    char buf[128];
    bool found_vco = false;
    for (int i = 0; i < count; ++i) {
        int rc = engine_get_module_type(engine, i, buf, sizeof(buf));
        EXPECT_EQ(rc, 0);
        if (std::strcmp(buf, "COMPOSITE_GENERATOR") == 0) found_vco = true;
    }
    EXPECT_TRUE(found_vco);
    engine_destroy(engine);
}

TEST_F(BridgeTest, AddModuleRejectsUnknownType) {
    EngineHandle engine = engine_create(sample_rate);
    ASSERT_NE(engine, nullptr);
    EXPECT_EQ(engine_add_module(engine, "DOES_NOT_EXIST", "VCO"), -1);
    engine_destroy(engine);
}

TEST_F(BridgeTest, BakeFailsWithEmptyChain) {
    EngineHandle engine = engine_create(sample_rate);
    ASSERT_NE(engine, nullptr);
    EXPECT_EQ(engine_bake(engine), -1); // no modules added
    engine_destroy(engine);
}

TEST_F(BridgeTest, ChainConstructionViaAPIProducesAudio) {
    EngineHandle engine = engine_create(sample_rate);
    ASSERT_NE(engine, nullptr);

    // Build: VCO → ENV → VCA  with an explicit gain_cv connection
    EXPECT_EQ(engine_add_module(engine, "COMPOSITE_GENERATOR", "VCO"), 0);
    EXPECT_EQ(engine_add_module(engine, "ADSR_ENVELOPE",        "ENV"), 0);
    EXPECT_EQ(engine_add_module(engine, "VCA",                  "VCA"), 0);
    EXPECT_EQ(engine_connect_ports(engine, "ENV", "envelope_out", "VCA", "gain_cv"), 0);
    EXPECT_EQ(engine_bake(engine), 0);

    // Set sine gain so the VCO produces output (default is all-zeros)
    set_param(engine, "sine_gain", 1.0f);

    // Trigger note and pull audio
    engine_note_on(engine, 60, 1.0f);
    std::vector<float> buffer(block_size * 2, 0.0f);
    engine_process(engine, buffer.data(), block_size);

    float peak = 0.0f;
    for (float s : buffer) peak = std::max(peak, std::abs(s));
    EXPECT_GT(peak, 0.0f);

    engine_destroy(engine);
}

// --- Phase 15: Patch v2 JSON load ---

// Locate the patches/ directory relative to the test binary.
static std::string find_patch(const std::string& name) {
    // Try a few common locations: alongside binary, in repo root.
    for (const char* base : {"patches", "../patches", "../../patches",
                              "../../../cxx/patches"}) {
        std::string p = std::string(base) + "/" + name;
        if (std::filesystem::exists(p)) return p;
    }
    return "";
}

TEST_F(BridgeTest, LoadPatchV2ShBass) {
    std::string patch = find_patch("sh_bass.json");
    if (patch.empty()) { GTEST_SKIP() << "patches/sh_bass.json not found"; }

    EngineHandle engine = engine_create(sample_rate);
    ASSERT_NE(engine, nullptr);
    EXPECT_EQ(engine_load_patch(engine, patch.c_str()), 0);

    engine_note_on(engine, 36, 1.0f); // C2 bass note
    std::vector<float> buffer(block_size * 2, 0.0f);
    engine_process(engine, buffer.data(), block_size);
    float peak = 0.0f;
    for (float s : buffer) peak = std::max(peak, std::abs(s));
    EXPECT_GT(peak, 0.0f);

    engine_destroy(engine);
}

TEST_F(BridgeTest, DrawbarOrganChainProducesAudio) {
    EngineHandle engine = engine_create(sample_rate);
    ASSERT_NE(engine, nullptr);

    EXPECT_EQ(engine_add_module(engine, "DRAWBAR_ORGAN", "ORGAN"), 0);
    EXPECT_EQ(engine_add_module(engine, "ADSR_ENVELOPE", "ENV"),   0);
    EXPECT_EQ(engine_add_module(engine, "VCA",           "VCA"),   0);
    EXPECT_EQ(engine_connect_ports(engine, "ENV", "envelope_out", "VCA", "gain_cv"), 0);
    EXPECT_EQ(engine_bake(engine), 0);

    // Open the 8' principal drawbar via named parameter
    set_param(engine, "drawbar_8", 8.0f);
    set_param(engine, "amp_attack", 0.0f);
    set_param(engine, "amp_sustain", 1.0f);

    engine_note_on(engine, 60, 1.0f);
    std::vector<float> buffer(block_size * 2, 0.0f);
    engine_process(engine, buffer.data(), block_size);

    float peak = 0.0f;
    for (float s : buffer) peak = std::max(peak, std::abs(s));
    EXPECT_GT(peak, 0.0f);

    engine_destroy(engine);
}

TEST_F(BridgeTest, LoadPatchV2OrganDrawbar) {
    std::string patch = find_patch("organ_drawbar.json");
    if (patch.empty()) { GTEST_SKIP() << "patches/organ_drawbar.json not found"; }

    EngineHandle engine = engine_create(sample_rate);
    ASSERT_NE(engine, nullptr);
    EXPECT_EQ(engine_load_patch(engine, patch.c_str()), 0);

    engine_note_on(engine, 60, 1.0f);
    std::vector<float> buffer(block_size * 2, 0.0f);
    engine_process(engine, buffer.data(), block_size);
    float peak = 0.0f;
    for (float s : buffer) peak = std::max(peak, std::abs(s));
    EXPECT_GT(peak, 0.0f);

    engine_destroy(engine);
}

TEST_F(BridgeTest, LoadPatchV2BadFileReturnsError) {
    EngineHandle engine = engine_create(sample_rate);
    ASSERT_NE(engine, nullptr);
    EXPECT_EQ(engine_load_patch(engine, "/nonexistent/path/patch.json"), -1);
    engine_destroy(engine);
}
