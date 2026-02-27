/**
 * @file PatchStore.hpp
 * @brief Human-readable JSON patch persistence system.
 */

#ifndef PATCH_STORE_HPP
#define PATCH_STORE_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "ModulationMatrix.hpp"

namespace audio {

using json = nlohmann::json;

/**
 * @brief Represents the full state of a synth patch.
 */
struct PatchData {
    int version = 1;
    std::string name;
    
    // Processor parameters
    std::unordered_map<std::string, float> parameters;
    
    // Modulation Matrix connections
    struct Connection {
        ModulationSource source;
        ModulationTarget target;
        float intensity;
    };
    std::vector<Connection> modulations;

    // JSON conversion
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(PatchData, version, name, parameters)
};

// Custom serialization for ModulationMatrix enums and Connection
inline void to_json(json& j, const PatchData::Connection& c) {
    j = json{
        {"source", static_cast<int>(c.source)},
        {"target", static_cast<int>(c.target)},
        {"intensity", c.intensity}
    };
}

inline void from_json(const json& j, PatchData::Connection& c) {
    c.source = static_cast<ModulationSource>(j.at("source").get<int>());
    c.target = static_cast<ModulationTarget>(j.at("target").get<int>());
    c.intensity = j.at("intensity").get<float>();
}

/**
 * @brief Manages saving and loading of PatchData.
 */
class PatchStore {
public:
    static bool save_to_file(const PatchData& patch, const std::string& path);
    static bool load_from_file(PatchData& patch, const std::string& path);
    
    /**
     * @brief Convert PatchData to JSON string.
     */
    static std::string serialize(const PatchData& patch) {
        json j = patch;
        // Manually add modulations due to custom enum serialization
        j["modulations"] = patch.modulations;
        return j.dump(4);
    }

    /**
     * @brief Load PatchData from JSON string.
     */
    static bool deserialize(PatchData& patch, const std::string& data) {
        try {
            json j = json::parse(data);
            patch = j.get<PatchData>();
            if (j.contains("modulations")) {
                patch.modulations = j.at("modulations").get<std::vector<PatchData::Connection>>();
            }
            return true;
        } catch (...) {
            return false;
        }
    }
};

} // namespace audio

#endif // PATCH_STORE_HPP
