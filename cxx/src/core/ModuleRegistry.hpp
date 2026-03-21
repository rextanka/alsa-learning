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
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace audio {

/**
 * @brief Role classification for a registered module type (Phase 27C).
 *
 * Inferred automatically from a module's declared PORT_AUDIO ports at
 * registration time. Used by patch editors and the introspection API
 * to categorise modules.
 *
 * Three roles only — SOURCE / SINK / PROCESSOR:
 *   SOURCE    — has PORT_AUDIO OUT, no PORT_AUDIO IN.
 *               Includes audio generators (VCO, WHITE_NOISE) and CV generators
 *               (LFO, ADSR, AD_ENVELOPE, SAMPLE_HOLD, GATE_DELAY).
 *               "Can be placed at the start of a chain."
 *   SINK      — has PORT_AUDIO IN, no PORT_AUDIO OUT (Phase 27C: AUDIO_OUTPUT,
 *               AUDIO_FILE_WRITER). "Terminates the audio chain."
 *   PROCESSOR — has both PORT_AUDIO IN and PORT_AUDIO OUT, OR has only
 *               PORT_CONTROL ports (CV transformers: INVERTER, CV_MIXER, MATHS).
 *               "Sits in the middle of a chain."
 *
 * CV generators (LFO, ADSR) are SOURCE despite having PORT_CONTROL IN ports:
 * the port-based inference treats PORT_AUDIO presence as the determinant.
 * Pure CV modules (no PORT_AUDIO at all) fall through to PROCESSOR, which a
 * patch editor can detect from the port listing.
 */
enum class ModuleRole {
    SOURCE,     ///< audio or CV generator — has audio_out (or only CV out); no audio_in
    SINK,       ///< audio terminal — has audio_in, no audio_out
    PROCESSOR   ///< everything else: audio transforms, CV transforms, routing
};

inline std::string_view module_role_name(ModuleRole r) noexcept {
    switch (r) {
        case ModuleRole::SOURCE:    return "SOURCE";
        case ModuleRole::SINK:      return "SINK";
        case ModuleRole::PROCESSOR: return "PROCESSOR";
    }
    return "PROCESSOR";
}

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

    ModuleRole role = ModuleRole::PROCESSOR; ///< inferred at registration (Phase 27C)
};

/**
 * @brief Infer the ModuleRole from a prototype processor's declared PORT_AUDIO ports.
 *
 * Only PORT_AUDIO ports determine the role:
 *   - audio_out, no audio_in  → SOURCE (generators and CV modules with only cv_out)
 *   - audio_in, no audio_out  → SINK
 *   - both, or neither        → PROCESSOR
 *
 * CV modules (LFO, ADSR, CV_MIXER, etc.) have no PORT_AUDIO ports, so they
 * fall through to PROCESSOR. The port listing distinguishes CV from audio
 * processors for patch editors that need finer categorisation.
 */
inline ModuleRole infer_role(const Processor& p) noexcept {
    bool audio_in = false, audio_out = false;
    for (const auto& port : p.ports()) {
        if (port.type != PortType::PORT_AUDIO) continue;
        if (port.dir == PortDirection::IN)  audio_in  = true;
        else                                audio_out = true;
    }
    if (audio_out && !audio_in)  return ModuleRole::SOURCE;
    if (audio_in  && !audio_out) return ModuleRole::SINK;
    return ModuleRole::PROCESSOR;
}

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
        desc.role        = infer_role(*prototype);

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
