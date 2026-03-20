/**
 * @file ModuleRegistry.hpp
 * @brief Singleton registry of all available DSP module types (Phase 15).
 *
 * Each processor .cpp file registers its module type via a static initializer:
 *
 *   static bool reg = ModuleRegistry::instance().register_module({
 *       "MOOG_FILTER",
 *       "4-pole Moog ladder low-pass filter",
 *       [](int sr) { return std::make_unique<MoogLadderProcessor>(sr); }
 *   });
 *
 * The registry is populated at library load time (before main() or engine_create()).
 * Callers use it to discover available modules and instantiate them by type name.
 *
 * RT-SAFE: The registry is read-only after library load. No allocations during audio.
 */

#ifndef MODULE_REGISTRY_HPP
#define MODULE_REGISTRY_HPP

#include "../dsp/Processor.hpp"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace audio {

/**
 * @brief Descriptor for a registered module type.
 *
 * Populated once per module type at static-init time; read-only thereafter.
 */
struct ModuleDescriptor {
    std::string type_name;    ///< e.g. "COMPOSITE_GENERATOR"
    std::string description;  ///< one-line human-readable description
    std::string usage_notes;  ///< longer-form usage guidance (Phase 26)

    /// Port layout — mirrored from a prototype instance's declare_port() calls.
    std::vector<PortDescriptor> ports;

    /// Parameter layout — mirrored from a prototype instance's declare_parameter() calls.
    std::vector<ParameterDescriptor> parameters;

    /// Factory: create a fresh processor instance at the given sample rate.
    std::function<std::unique_ptr<Processor>(int sample_rate)> factory;
};

/**
 * @brief Meyers-singleton registry of all available module types.
 *
 * Thread-safety: registration happens at static-init time (single-threaded).
 * Post-init reads are lock-free (no writes after main() starts).
 */
class ModuleRegistry {
public:
    static ModuleRegistry& instance() {
        static ModuleRegistry inst;
        return inst;
    }

    /**
     * @brief Register a module type. Called via static initializer in processor .cpp.
     *
     * Builds a prototype instance at sample_rate=48000 to harvest its declared
     * ports and parameters, then stores everything in the descriptor.
     *
     * @param type_name   Unique type string (e.g. "MOOG_FILTER")
     * @param description One-line human-readable description
     * @param usage_notes Longer-form usage guidance (patch examples, tips, limits).
     *                    Defaults to "" for call sites that pre-date Phase 26.
     * @param factory     Factory function: (int sample_rate) → unique_ptr<Processor>
     * @return true (for use in static bool initializer idiom)
     */
    bool register_module(
        std::string type_name,
        std::string description,
        std::string usage_notes,
        std::function<std::unique_ptr<Processor>(int)> factory)
    {
        // Build a prototype at a neutral sample rate to harvest declared ports/params.
        auto prototype = factory(48000);

        // Preserve existing usage_notes if the new registration provides none.
        // register_builtin_processors() uses the 3-arg overload (usage_notes="")
        // and runs after the per-processor kRegistered statics (4-arg, with notes).
        // Without this guard the 3-arg re-registration would wipe the notes out,
        // breaking Phase 27A introspection.
        if (usage_notes.empty()) {
            auto it = registry_.find(type_name);
            if (it != registry_.end() && !it->second.usage_notes.empty())
                usage_notes = it->second.usage_notes;
        }

        ModuleDescriptor desc;
        desc.type_name   = type_name;
        desc.description = std::move(description);
        desc.usage_notes = std::move(usage_notes);
        desc.ports       = prototype->ports();
        desc.parameters  = prototype->parameters();
        desc.factory     = std::move(factory);

        registry_[std::move(type_name)] = std::move(desc);
        return true;
    }

    /**
     * @brief Overload without usage_notes for backward compatibility.
     */
    bool register_module(
        std::string type_name,
        std::string description,
        std::function<std::unique_ptr<Processor>(int)> factory)
    {
        return register_module(std::move(type_name), std::move(description),
                               /*usage_notes=*/"", std::move(factory));
    }

    /**
     * @brief Look up a module descriptor by type name. Returns nullptr if not found.
     */
    const ModuleDescriptor* find(const std::string& type_name) const {
        auto it = registry_.find(type_name);
        return it != registry_.end() ? &it->second : nullptr;
    }

    /**
     * @brief Create a new processor instance of the given type.
     * Returns nullptr if type_name is not registered.
     */
    std::unique_ptr<Processor> create(const std::string& type_name, int sample_rate) const {
        const auto* desc = find(type_name);
        return desc ? desc->factory(sample_rate) : nullptr;
    }

    /**
     * @brief All registered module type names (stable iteration order not guaranteed).
     */
    std::vector<std::string> type_names() const {
        std::vector<std::string> names;
        names.reserve(registry_.size());
        for (const auto& [k, _] : registry_) names.push_back(k);
        return names;
    }

    size_t size() const { return registry_.size(); }

private:
    ModuleRegistry() = default;
    ModuleRegistry(const ModuleRegistry&) = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;

    std::unordered_map<std::string, ModuleDescriptor> registry_;
};

/**
 * @brief Register all built-in processor types into the singleton registry.
 *
 * Implemented in ProcessorRegistrations.cpp. Call once from engine_create()
 * and from any unit test that needs the registry populated. Idempotent.
 */
void register_builtin_processors();

} // namespace audio

#endif // MODULE_REGISTRY_HPP
