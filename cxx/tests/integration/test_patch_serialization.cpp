/**
 * @file test_patch_serialization.cpp
 * @brief Integration tests for Phase 27B: engine_get_patch_json /
 *        engine_load_patch_json / engine_save_patch.
 *
 * Tests:
 *   1. RoundTrip       — load juno_pad.json, serialize, reload via
 *                        engine_load_patch_json, play note, assert non-silent.
 *   2. ModSources      — LFO modulation survives the round-trip (juno_pad has
 *                        LFO1 → VCO pitch_cv; confirm the connection is present
 *                        in the serialized JSON).
 *   3. PostChainRoundTrip — push REVERB_FDN onto post-chain, serialize, reload,
 *                           verify post_chain is present with correct type.
 *   4. SavePatch       — engine_save_patch writes a valid file that can be
 *                        reloaded via engine_load_patch.
 *   5. BufferTooSmall  — engine_get_patch_json returns -2 when buf is too small.
 *   6. JunoStringsFix  — juno_strings.json loads without error (JUNO_CHORUS now
 *                        in post_chain, not voice chain).
 */

#include <gtest/gtest.h>
#include "CInterface.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <cmath>
#include <filesystem>
#include <fstream>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

static std::string find_patch(const std::string& name) {
    for (const char* base : {"patches", "../patches", "../../patches",
                              "../../../cxx/patches"}) {
        std::string p = std::string(base) + "/" + name;
        if (std::filesystem::exists(p)) return p;
    }
    return "";
}

// Render N blocks of 128 frames and return peak absolute amplitude.
static float render_peak(EngineHandle engine, int blocks = 200) {
    constexpr size_t kBlock = 128;
    std::vector<float> buf(kBlock * 2, 0.0f);
    float peak = 0.0f;
    for (int i = 0; i < blocks; ++i) {
        engine_process(engine, buf.data(), kBlock);
        for (float s : buf) peak = std::max(peak, std::abs(s));
    }
    return peak;
}

// Serialize current patch to a std::string.
static std::string get_json(EngineHandle engine) {
    std::vector<char> buf(65536);
    int n = engine_get_patch_json(engine, 0, buf.data(), static_cast<int>(buf.size()));
    EXPECT_GT(n, 0) << "engine_get_patch_json returned error " << n;
    return std::string(buf.data(), static_cast<size_t>(n));
}

} // namespace

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class PatchSerializationTest : public ::testing::Test {
protected:
    static constexpr unsigned int kSR = 44100;
    static constexpr size_t       kBlock = 128;
};

// ---------------------------------------------------------------------------
// Test 1: Basic round-trip — load file, serialize, reload from string
// ---------------------------------------------------------------------------

TEST_F(PatchSerializationTest, RoundTrip) {
    const std::string path = find_patch("juno_pad.json");
    if (path.empty()) GTEST_SKIP() << "juno_pad.json not found";

    // --- Original engine ---
    EngineHandle orig = engine_create(kSR);
    ASSERT_NE(orig, nullptr);
    ASSERT_EQ(engine_load_patch(orig, path.c_str()), 0);

    engine_note_on(orig, 60, 1.0f);
    float orig_peak = render_peak(orig);
    engine_destroy(orig);

    ASSERT_GT(orig_peak, 1e-4f) << "original patch must produce audio";

    // --- Re-load via serialized JSON ---
    EngineHandle orig2 = engine_create(kSR);
    ASSERT_NE(orig2, nullptr);
    ASSERT_EQ(engine_load_patch(orig2, path.c_str()), 0);
    const std::string json_str = get_json(orig2);
    engine_destroy(orig2);

    ASSERT_FALSE(json_str.empty());

    // Parse and verify version bumped to 3
    auto j = nlohmann::json::parse(json_str);
    EXPECT_EQ(j.value("version", 0), 3);
    EXPECT_TRUE(j.contains("groups"));

    // --- Reload from string ---
    EngineHandle reloaded = engine_create(kSR);
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(engine_load_patch_json(reloaded, json_str.c_str(),
                                     static_cast<int>(json_str.size())), 0);

    engine_note_on(reloaded, 60, 1.0f);
    float reload_peak = render_peak(reloaded);
    engine_destroy(reloaded);

    EXPECT_GT(reload_peak, 1e-4f) << "reloaded patch must produce audio";
}

// ---------------------------------------------------------------------------
// Test 2: mod_sources_ coverage — LFO connection survives round-trip
// ---------------------------------------------------------------------------

TEST_F(PatchSerializationTest, ModSources) {
    const std::string path = find_patch("juno_pad.json");
    if (path.empty()) GTEST_SKIP() << "juno_pad.json not found";

    EngineHandle engine = engine_create(kSR);
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine_load_patch(engine, path.c_str()), 0);

    const std::string json_str = get_json(engine);
    engine_destroy(engine);

    auto j = nlohmann::json::parse(json_str);
    const auto& group = j["groups"][0];

    // LFO1 must appear in the chain
    bool has_lfo = false;
    for (const auto& entry : group["chain"]) {
        if (entry.value("type", "") == "LFO" && entry.value("tag", "") == "LFO1")
            has_lfo = true;
    }
    EXPECT_TRUE(has_lfo) << "LFO1 must be serialized in chain";

    // LFO1.control_out → VCO.pitch_cv connection must be present
    bool has_lfo_conn = false;
    for (const auto& c : group["connections"]) {
        if (c.value("from_tag", "") == "LFO1" &&
            c.value("from_port", "") == "control_out" &&
            c.value("to_tag", "") == "VCO" &&
            c.value("to_port", "") == "pitch_cv") {
            has_lfo_conn = true;
        }
    }
    EXPECT_TRUE(has_lfo_conn) << "LFO1.control_out → VCO.pitch_cv must be serialized";
}

// ---------------------------------------------------------------------------
// Test 3: post_chain round-trip
// ---------------------------------------------------------------------------

TEST_F(PatchSerializationTest, PostChainRoundTrip) {
    EngineHandle engine = engine_create(kSR);
    ASSERT_NE(engine, nullptr);

    // Add a post-chain reverb and set a parameter
    int idx = engine_post_chain_push(engine, "REVERB_FDN");
    ASSERT_GE(idx, 0) << "REVERB_FDN must be a registered post-chain type";
    EXPECT_EQ(engine_post_chain_set_param(engine, idx, "wet", 0.35f), 0);

    const std::string json_str = get_json(engine);
    engine_destroy(engine);

    // Verify post_chain present in JSON
    auto j = nlohmann::json::parse(json_str);
    ASSERT_TRUE(j.contains("post_chain")) << "post_chain must be serialized";
    ASSERT_FALSE(j["post_chain"].empty());
    EXPECT_EQ(j["post_chain"][0].value("type", ""), "REVERB_FDN");
    EXPECT_NEAR(j["post_chain"][0]["parameters"].value("wet", 0.0f), 0.35f, 1e-4f);

    // Reload and verify post_chain was restored
    EngineHandle reloaded = engine_create(kSR);
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(engine_load_patch_json(reloaded, json_str.c_str(),
                                     static_cast<int>(json_str.size())), 0);

    // Re-serialize and confirm post_chain survived
    const std::string json2 = get_json(reloaded);
    engine_destroy(reloaded);

    auto j2 = nlohmann::json::parse(json2);
    ASSERT_TRUE(j2.contains("post_chain"));
    EXPECT_EQ(j2["post_chain"][0].value("type", ""), "REVERB_FDN");
}

// ---------------------------------------------------------------------------
// Test 4: engine_save_patch / reload via engine_load_patch
// ---------------------------------------------------------------------------

TEST_F(PatchSerializationTest, SavePatch) {
    const std::string src = find_patch("juno_pad.json");
    if (src.empty()) GTEST_SKIP() << "juno_pad.json not found";

    const std::string tmp = (std::filesystem::temp_directory_path()
                             / "test_save_patch_27b.json").string();

    EngineHandle engine = engine_create(kSR);
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine_load_patch(engine, src.c_str()), 0);
    EXPECT_EQ(engine_save_patch(engine, tmp.c_str()), 0);
    engine_destroy(engine);

    ASSERT_TRUE(std::filesystem::exists(tmp)) << "saved file must exist";

    // Reload the saved file
    EngineHandle reloaded = engine_create(kSR);
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(engine_load_patch(reloaded, tmp.c_str()), 0);

    engine_note_on(reloaded, 60, 1.0f);
    float peak = render_peak(reloaded);
    engine_destroy(reloaded);

    EXPECT_GT(peak, 1e-4f) << "saved+reloaded patch must produce audio";

    std::filesystem::remove(tmp);
}

// ---------------------------------------------------------------------------
// Test 5: buffer-too-small returns -2
// ---------------------------------------------------------------------------

TEST_F(PatchSerializationTest, BufferTooSmall) {
    EngineHandle engine = engine_create(kSR);
    ASSERT_NE(engine, nullptr);

    char tiny[4] = {};
    EXPECT_EQ(engine_get_patch_json(engine, 0, tiny, sizeof(tiny)), -2);

    engine_destroy(engine);
}

// ---------------------------------------------------------------------------
// Test 6: juno_strings.json loads cleanly (JUNO_CHORUS moved to post_chain)
// ---------------------------------------------------------------------------

TEST_F(PatchSerializationTest, JunoStringsFix) {
    const std::string path = find_patch("juno_strings.json");
    if (path.empty()) GTEST_SKIP() << "juno_strings.json not found";

    EngineHandle engine = engine_create(kSR);
    ASSERT_NE(engine, nullptr);
    EXPECT_EQ(engine_load_patch(engine, path.c_str()), 0);

    // Voice chain must NOT contain JUNO_CHORUS
    const std::string json_str = get_json(engine);
    engine_destroy(engine);

    auto j = nlohmann::json::parse(json_str);
    for (const auto& entry : j["groups"][0]["chain"]) {
        EXPECT_NE(entry.value("type", ""), "JUNO_CHORUS")
            << "JUNO_CHORUS must not appear in voice chain after fix";
    }
    // JUNO_CHORUS must be in post_chain
    ASSERT_TRUE(j.contains("post_chain")) << "post_chain must be present in juno_strings";
    bool found = false;
    for (const auto& fx : j["post_chain"])
        if (fx.value("type", "") == "JUNO_CHORUS") found = true;
    EXPECT_TRUE(found) << "JUNO_CHORUS must be in post_chain";
}
