/**
 * @file test_module_registry.cpp
 * @brief Unit tests for ModuleRegistry and Phase 27A JSON introspection.
 *
 * All assertions are structural — no module names or counts are hardcoded.
 * Tests remain valid as modules are added or removed.
 */

#include <gtest/gtest.h>
#include "ModuleRegistry.hpp"   // also declares register_builtin_processors()
#include <nlohmann/json.hpp>
#include "CInterface.h"

// ---------------------------------------------------------------------------
// Fixture: ensure registry is populated once for all tests in this suite.
// ---------------------------------------------------------------------------

class ModuleRegistryTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        audio::register_builtin_processors();
    }
};

// ---------------------------------------------------------------------------
// Registry population
// ---------------------------------------------------------------------------

TEST_F(ModuleRegistryTest, AtLeastOneModuleRegistered) {
    EXPECT_GT(audio::ModuleRegistry::instance().size(), 0u);
}

TEST_F(ModuleRegistryTest, AllTypeNamesNonEmpty) {
    for (const auto& name : audio::ModuleRegistry::instance().type_names()) {
        EXPECT_FALSE(name.empty()) << "Empty type_name found in registry";
    }
}

// ---------------------------------------------------------------------------
// Per-descriptor structural invariants
// ---------------------------------------------------------------------------

TEST_F(ModuleRegistryTest, EachDescriptorHasNonEmptyBrief) {
    for (const auto& name : audio::ModuleRegistry::instance().type_names()) {
        const auto* desc = audio::ModuleRegistry::instance().find(name);
        ASSERT_NE(desc, nullptr) << "find() returned nullptr for registered name: " << name;
        EXPECT_FALSE(desc->description.empty())
            << name << ": description (brief) must not be empty";
    }
}

TEST_F(ModuleRegistryTest, EachDescriptorHasAtLeastOnePort) {
    for (const auto& name : audio::ModuleRegistry::instance().type_names()) {
        const auto* desc = audio::ModuleRegistry::instance().find(name);
        ASSERT_NE(desc, nullptr);
        EXPECT_FALSE(desc->ports.empty())
            << name << ": must declare at least one port";
    }
}

TEST_F(ModuleRegistryTest, EachDescriptorHasUsageNotes) {
    for (const auto& name : audio::ModuleRegistry::instance().type_names()) {
        const auto* desc = audio::ModuleRegistry::instance().find(name);
        ASSERT_NE(desc, nullptr);
        EXPECT_FALSE(desc->usage_notes.empty())
            << name << ": usage_notes must be non-empty (required for Phase 27A introspection)";
    }
}

TEST_F(ModuleRegistryTest, EveryPortHasNameAndValidType) {
    for (const auto& name : audio::ModuleRegistry::instance().type_names()) {
        const auto* desc = audio::ModuleRegistry::instance().find(name);
        ASSERT_NE(desc, nullptr);
        for (const auto& port : desc->ports) {
            EXPECT_FALSE(port.name.empty())
                << name << ": port has empty name";
            EXPECT_TRUE(port.type == audio::PortType::PORT_AUDIO ||
                        port.type == audio::PortType::PORT_CONTROL)
                << name << ": port '" << port.name << "' has invalid type";
        }
    }
}

TEST_F(ModuleRegistryTest, EveryParameterHasNameAndValidRange) {
    for (const auto& name : audio::ModuleRegistry::instance().type_names()) {
        const auto* desc = audio::ModuleRegistry::instance().find(name);
        ASSERT_NE(desc, nullptr);
        for (const auto& param : desc->parameters) {
            EXPECT_FALSE(param.name.empty())
                << name << ": parameter has empty name";
            EXPECT_LE(param.min, param.max)
                << name << ": parameter '" << param.name << "' has min > max";
            EXPECT_GE(param.def, param.min)
                << name << ": parameter '" << param.name << "' default < min";
            EXPECT_LE(param.def, param.max)
                << name << ": parameter '" << param.name << "' default > max";
        }
    }
}

TEST_F(ModuleRegistryTest, FactoryProducesValidInstanceAtArbitrarySampleRate) {
    for (const auto& name : audio::ModuleRegistry::instance().type_names()) {
        const auto* desc = audio::ModuleRegistry::instance().find(name);
        ASSERT_NE(desc, nullptr);
        ASSERT_TRUE(desc->factory) << name << ": factory is null";
        auto instance = desc->factory(44100);
        EXPECT_NE(instance, nullptr) << name << ": factory returned nullptr at 44100 Hz";
        instance = desc->factory(48000);
        EXPECT_NE(instance, nullptr) << name << ": factory returned nullptr at 48000 Hz";
    }
}

// ---------------------------------------------------------------------------
// Phase 27A: C API — module_get_descriptor_json
// ---------------------------------------------------------------------------

TEST_F(ModuleRegistryTest, GetDescriptorJsonUnknownTypeReturnsMinusOne) {
    char buf[256];
    EXPECT_EQ(module_get_descriptor_json("NOT_A_MODULE", buf, sizeof(buf)), -1);
}

TEST_F(ModuleRegistryTest, GetDescriptorJsonBufTooSmallReturnsMinusTwo) {
    // Use the first registered type name as a known-good key.
    const auto names = audio::ModuleRegistry::instance().type_names();
    ASSERT_FALSE(names.empty());
    char tiny[4];
    EXPECT_EQ(module_get_descriptor_json(names[0].c_str(), tiny, sizeof(tiny)), -2);
}

TEST_F(ModuleRegistryTest, GetDescriptorJsonProducesValidJsonForEveryModule) {
    for (const auto& name : audio::ModuleRegistry::instance().type_names()) {
        char buf[8192];
        const int n = module_get_descriptor_json(name.c_str(), buf, sizeof(buf));
        ASSERT_GE(n, 0) << name << ": module_get_descriptor_json returned error " << n;

        nlohmann::json j;
        ASSERT_NO_THROW(j = nlohmann::json::parse(buf))
            << name << ": JSON parse failed";

        EXPECT_EQ(j.value("type_name", ""), name);
        EXPECT_FALSE(j.value("brief", std::string{}).empty())       << name << ": brief is empty";
        EXPECT_FALSE(j.value("usage_notes", std::string{}).empty()) << name << ": usage_notes is empty";
        EXPECT_TRUE(j.contains("parameters") && j["parameters"].is_array());
        EXPECT_TRUE(j.contains("ports")      && j["ports"].is_array());
        EXPECT_FALSE(j["ports"].empty())                             << name << ": ports array is empty";
    }
}

// ---------------------------------------------------------------------------
// Phase 27A: C API — module_registry_get_all_json
// ---------------------------------------------------------------------------

TEST_F(ModuleRegistryTest, GetAllJsonBufTooSmallReturnsMinusTwo) {
    char tiny[4];
    EXPECT_EQ(module_registry_get_all_json(tiny, sizeof(tiny)), -2);
}

TEST_F(ModuleRegistryTest, GetAllJsonProducesArrayWithCorrectCount) {
    constexpr int kBufSize = 1 << 20; // 1 MiB — plenty for all 30+ modules
    std::vector<char> buf(kBufSize);
    const int n = module_registry_get_all_json(buf.data(), kBufSize);
    ASSERT_GE(n, 0) << "module_registry_get_all_json returned error " << n;

    nlohmann::json arr;
    ASSERT_NO_THROW(arr = nlohmann::json::parse(buf.data()));
    ASSERT_TRUE(arr.is_array());

    const size_t registry_count = audio::ModuleRegistry::instance().size();
    EXPECT_EQ(arr.size(), registry_count)
        << "JSON array length does not match registry size";
}

TEST_F(ModuleRegistryTest, GetAllJsonIsSortedAlphabetically) {
    constexpr int kBufSize = 1 << 20;
    std::vector<char> buf(kBufSize);
    const int n = module_registry_get_all_json(buf.data(), kBufSize);
    ASSERT_GE(n, 0);

    const auto arr = nlohmann::json::parse(buf.data());
    ASSERT_TRUE(arr.is_array());

    std::string prev;
    for (const auto& entry : arr) {
        const std::string cur = entry.value("type_name", "");
        EXPECT_LT(prev, cur) << "Array is not sorted: '" << prev << "' >= '" << cur << "'";
        prev = cur;
    }
}

TEST_F(ModuleRegistryTest, GetAllJsonEachEntryMatchesSingleDescriptorLookup) {
    constexpr int kBufSize = 1 << 20;
    std::vector<char> buf(kBufSize);
    ASSERT_GE(module_registry_get_all_json(buf.data(), kBufSize), 0);

    const auto arr = nlohmann::json::parse(buf.data());
    for (const auto& entry : arr) {
        const std::string name = entry.value("type_name", "");
        ASSERT_FALSE(name.empty());

        char single[8192];
        ASSERT_GE(module_get_descriptor_json(name.c_str(), single, sizeof(single)), 0)
            << "Single lookup failed for " << name;

        const auto single_j = nlohmann::json::parse(single);
        EXPECT_EQ(entry["type_name"], single_j["type_name"]);
        EXPECT_EQ(entry["brief"],     single_j["brief"]);
        EXPECT_EQ(entry["ports"],     single_j["ports"]);
    }
}
