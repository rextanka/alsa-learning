/**
 * @file test_module_introspection.cpp
 * @brief Integration tests for Phase 27A: module_get_descriptor_json /
 *        module_registry_get_all_json C API.
 *
 * Uses only CInterface.h (no C++ internals).  Module names and counts are
 * never hardcoded — all assertions are structural invariants that hold for
 * any valid registry.
 */

#include <gtest/gtest.h>
#include "CInterface.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/// Retrieve the full JSON array and parse it.  Fatal-asserts on error.
nlohmann::json get_all_json() {
    // First call with a tiny buffer to confirm -2 is returned (not a crash).
    char probe[4];
    EXPECT_EQ(module_registry_get_all_json(probe, sizeof(probe)), -2);

    // Real call with a 1 MiB buffer — sufficient for any realistic registry.
    constexpr int kBuf = 1 << 20;
    std::vector<char> buf(kBuf);
    const int n = module_registry_get_all_json(buf.data(), kBuf);
    EXPECT_GE(n, 0) << "module_registry_get_all_json returned error " << n;

    nlohmann::json arr;
    EXPECT_NO_THROW(arr = nlohmann::json::parse(buf.data()));
    return arr;
}

/// Retrieve and parse the JSON descriptor for a single type.
/// Returns an empty object on error and records a test failure.
nlohmann::json get_descriptor(const std::string& type_name) {
    char buf[8192];
    const int n = module_get_descriptor_json(type_name.c_str(), buf, sizeof(buf));
    EXPECT_GE(n, 0) << "module_get_descriptor_json(\"" << type_name << "\") returned " << n;
    if (n < 0) return {};
    nlohmann::json j;
    EXPECT_NO_THROW(j = nlohmann::json::parse(buf));
    return j;
}

} // namespace

// ---------------------------------------------------------------------------
// module_registry_get_all_json — bulk registry
// ---------------------------------------------------------------------------

TEST(ModuleIntrospection, AllJsonIsArray) {
    const auto arr = get_all_json();
    ASSERT_TRUE(arr.is_array());
}

TEST(ModuleIntrospection, AllJsonArrayIsNonEmpty) {
    const auto arr = get_all_json();
    ASSERT_FALSE(arr.empty()) << "Registry returned empty module array";
}

TEST(ModuleIntrospection, AllJsonSortedAlphabetically) {
    const auto arr = get_all_json();
    std::string prev;
    for (const auto& entry : arr) {
        std::string cur = entry.value("type_name", "");
        EXPECT_LT(prev, cur) << "Array not sorted at: '" << cur << "'";
        prev = cur;
    }
}

TEST(ModuleIntrospection, AllJsonEachEntryHasRequiredTopLevelKeys) {
    const auto arr = get_all_json();
    for (const auto& entry : arr) {
        const std::string name = entry.value("type_name", "");
        ASSERT_FALSE(name.empty())            << "entry missing type_name";
        EXPECT_FALSE(entry.value("brief", std::string{}).empty())
                                              << name << ": brief is empty";
        EXPECT_TRUE(entry.contains("usage_notes"))
                                              << name << ": missing usage_notes key";
        EXPECT_TRUE(entry.contains("parameters") && entry["parameters"].is_array())
                                              << name << ": parameters must be array";
        EXPECT_TRUE(entry.contains("ports")   && entry["ports"].is_array())
                                              << name << ": ports must be array";
        EXPECT_FALSE(entry["ports"].empty())  << name << ": ports array is empty";
        // Phase 27C: role field
        ASSERT_TRUE(entry.contains("role"))   << name << ": missing role field";
        const std::string role = entry.value("role", "");
        const bool valid_role = (role == "SOURCE" || role == "SINK" || role == "PROCESSOR");
        EXPECT_TRUE(valid_role)               << name << ": unknown role '" << role << "'";
    }
}

// Phase 27C: verify specific well-known roles (3-role model: SOURCE/SINK/PROCESSOR)
TEST(ModuleIntrospection, KnownModuleRoles) {
    static const std::pair<const char*, const char*> kExpected[] = {
        // COMPOSITE_GENERATOR has fm_in (PORT_AUDIO IN) + audio_out → PROCESSOR
        {"COMPOSITE_GENERATOR", "PROCESSOR"},
        // WHITE_NOISE has only audio_out (no PORT_AUDIO IN) → SOURCE
        {"WHITE_NOISE",         "SOURCE"},
        // Phase 27C I/O processors
        {"AUDIO_OUTPUT",        "SINK"},
        {"AUDIO_INPUT",         "SOURCE"},
        {"AUDIO_FILE_READER",   "SOURCE"},
        {"AUDIO_FILE_WRITER",   "SINK"},
        // Audio processors
        {"MOOG_FILTER",         "PROCESSOR"},
        {"VCA",                 "PROCESSOR"},
        // LFO/ADSR have only PORT_CONTROL ports → no PORT_AUDIO → PROCESSOR
        // (patch editors distinguish CV modules from audio processors via port listing)
        {"LFO",                 "PROCESSOR"},
        {"ADSR_ENVELOPE",       "PROCESSOR"},
    };
    for (const auto& [type, expected_role] : kExpected) {
        const auto d = get_descriptor(type);
        EXPECT_EQ(d.value("role", ""), expected_role)
            << type << " expected role " << expected_role;
    }
}

TEST(ModuleIntrospection, AllJsonEachPortHasRequiredFields) {
    const auto arr = get_all_json();
    for (const auto& entry : arr) {
        const std::string mod = entry.value("type_name", "");
        for (const auto& port : entry["ports"]) {
            EXPECT_FALSE(port.value("name",      std::string{}).empty()) << mod << ": port.name empty";
            EXPECT_FALSE(port.value("type",      std::string{}).empty()) << mod << ": port.type empty";
            EXPECT_FALSE(port.value("direction", std::string{}).empty()) << mod << ": port.direction empty";
            // type must be one of the two legal values
            const std::string pt = port.value("type", "");
            EXPECT_TRUE(pt == "PORT_AUDIO" || pt == "PORT_CONTROL")
                << mod << ": port.type '" << pt << "' is not a recognised value";
            // direction must be one of the two legal values
            const std::string dir = port.value("direction", "");
            EXPECT_TRUE(dir == "PORT_INPUT" || dir == "PORT_OUTPUT")
                << mod << ": port.direction '" << dir << "' is not a recognised value";
        }
    }
}

TEST(ModuleIntrospection, AllJsonEachParameterHasRequiredFields) {
    const auto arr = get_all_json();
    for (const auto& entry : arr) {
        const std::string mod = entry.value("type_name", "");
        for (const auto& param : entry["parameters"]) {
            EXPECT_FALSE(param.value("name", std::string{}).empty())
                << mod << ": parameter.name empty";
            EXPECT_TRUE(param.contains("min") && param.contains("max") && param.contains("default"))
                << mod << ": parameter missing min/max/default";
            EXPECT_LE(param.value("min", 0.0f), param.value("max", 0.0f))
                << mod << ": parameter '" << param.value("name","?") << "' min > max";
        }
    }
}

// ---------------------------------------------------------------------------
// module_get_descriptor_json — single module lookup
// ---------------------------------------------------------------------------

TEST(ModuleIntrospection, SingleLookupUnknownTypeReturnsMinusOne) {
    char buf[256];
    EXPECT_EQ(module_get_descriptor_json("DOES_NOT_EXIST", buf, sizeof(buf)), -1);
}

TEST(ModuleIntrospection, SingleLookupNullTypeReturnsMinusOne) {
    char buf[256];
    EXPECT_EQ(module_get_descriptor_json(nullptr, buf, sizeof(buf)), -1);
}

TEST(ModuleIntrospection, SingleLookupTinyBufferReturnsMinusTwo) {
    // Use the first module from the all-modules array as a known-good key.
    const auto arr = get_all_json();
    ASSERT_FALSE(arr.empty());
    const std::string first = arr[0].value("type_name", "");
    char tiny[4];
    EXPECT_EQ(module_get_descriptor_json(first.c_str(), tiny, sizeof(tiny)), -2);
}

TEST(ModuleIntrospection, SingleLookupMatchesBulkArrayForEveryModule) {
    // For every module returned by get_all, the single-lookup descriptor must
    // match on type_name, brief, and ports — ensuring consistency between the
    // two code paths.
    const auto arr = get_all_json();
    for (const auto& bulk_entry : arr) {
        const std::string name = bulk_entry.value("type_name", "");
        const auto single = get_descriptor(name);
        if (single.empty()) continue; // failure already recorded by get_descriptor()

        EXPECT_EQ(bulk_entry["type_name"],   single["type_name"])  << name;
        EXPECT_EQ(bulk_entry["brief"],       single["brief"])       << name;
        EXPECT_EQ(bulk_entry["usage_notes"], single["usage_notes"]) << name;
        EXPECT_EQ(bulk_entry["ports"],       single["ports"])       << name;
        EXPECT_EQ(bulk_entry["parameters"],  single["parameters"])  << name;
    }
}
