/**
 * @file AudioBridge.cpp 
 * @brief C-compatible API bridge for oscillator processors.
 */

#include "CInterface.h"
#include "OscillatorProcessor.hpp"
#include "SineOscillatorProcessor.hpp"
#include "SquareOscillatorProcessor.hpp"
#include "SawtoothOscillatorProcessor.hpp"
#include "TriangleOscillatorProcessor.hpp"
#include "WavetableOscillatorProcessor.hpp"
#include "AdsrEnvelopeProcessor.hpp"
#include "ADEnvelopeProcessor.hpp"
#include "MoogLadderProcessor.hpp"
#include "DiodeLadderProcessor.hpp"
#include "DelayLine.hpp"
#include "AudioTap.hpp"
#include "JunoChorus.hpp"
#include "../dsp/fx/FreeverbProcessor.hpp"
#include "../dsp/fx/FdnReverbProcessor.hpp"
#include "../dsp/fx/PhaserProcessor.hpp"
#include "VoiceManager.hpp"
#include "AudioDriver.hpp"
#ifdef __APPLE__
#include "coreaudio/CoreAudioDriver.hpp"
#else
#include "alsa/AlsaDriver.hpp"
#endif
#include "MusicalClock.hpp"
#include "TuningSystem.hpp"
#include "Logger.hpp"
#include "ModuleRegistry.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include "SummingBus.hpp"
#include "SmfParser.hpp"
#include "MidiFilePlayer.hpp"
#include <memory>
#include <span>
#include <cassert>
#include <cstring>

// Handle type discrimination for tag-based safety
enum class HandleType {
    Oscillator,
    Envelope,
    Engine,
    GenericProcessor
};

struct HandleBase {
    HandleType type;
    explicit HandleBase(HandleType t) : type(t) {}
    virtual ~HandleBase() = default;
};

// Internal handle structure (hidden from C API)
struct OscillatorHandleImpl : public HandleBase {
    std::unique_ptr<audio::Processor> processor;
    int sample_rate;
    
    OscillatorHandleImpl(std::unique_ptr<audio::Processor> proc, int sr)
        : HandleBase(HandleType::Oscillator)
        , processor(std::move(proc))
        , sample_rate(sr)
    {
    }
};

struct EnvelopeHandleImpl : public HandleBase {
    std::unique_ptr<audio::EnvelopeProcessor> processor;
    int sample_rate;

    EnvelopeHandleImpl(std::unique_ptr<audio::EnvelopeProcessor> proc, int sr)
        : HandleBase(HandleType::Envelope)
        , processor(std::move(proc))
        , sample_rate(sr)
    {
    }
};

struct EngineHandleImpl : public HandleBase {
    std::unique_ptr<audio::VoiceManager> voice_manager;
    std::unique_ptr<hal::AudioDriver> driver;
    std::unique_ptr<audio::SummingBus> summing_bus;
    std::unique_ptr<audio::AudioTap> tap;
    std::unique_ptr<audio::JunoChorus> chorus;
    bool chorus_enabled = false;
    audio::MusicalClock clock;
    audio::TwelveToneEqual tuning;
    int sample_rate;
    // Phase 15: pending chain spec (built up by engine_add_module / engine_connect_ports)
    struct ModuleSpec { std::string type_name; std::string tag; };
    std::vector<ModuleSpec> pending_modules;
    std::vector<audio::Voice::PortConnection> pending_connections;

    // Phase 22A: SMF file sequencer
    audio::MidiFilePlayer midi_player;

    // Phase 19: global post-processing chain (applied after voice summing + chorus).
    // Each element is a stereo Processor driven via pull(AudioBuffer&).
    std::vector<std::unique_ptr<audio::Processor>> post_chain;

    // Scratch buffers for engine_process stereo path (avoids per-call allocation).
    std::vector<float> process_left;
    std::vector<float> process_right;

    virtual ~EngineHandleImpl() {
        if (driver) driver->stop();
    }

    EngineHandleImpl(int sr)
        : HandleBase(HandleType::Engine)
        , voice_manager(std::make_unique<audio::VoiceManager>(sr))
        , summing_bus(std::make_unique<audio::SummingBus>(512))
        , tap(std::make_unique<audio::AudioTap>(16384))
        , chorus(std::make_unique<audio::JunoChorus>(static_cast<double>(sr)))
        , clock(static_cast<double>(sr))
        , sample_rate(sr)
    {

#ifdef __APPLE__
        driver = std::make_unique<hal::CoreAudioDriver>(sr, 512);
#else
        driver = std::make_unique<hal::AlsaDriver>(sr, 512);
#endif
    // Link the driver to the voice manager (which uses SummingBus)
    driver->set_stereo_callback([this](audio::AudioBuffer& buffer) {
        if (!voice_manager) return;
        
        // Advance clock and SMF sequencer by this block's frame count.
        const uint32_t block_frames = static_cast<uint32_t>(buffer.frames());
        clock.advance(static_cast<int32_t>(block_frames));
        midi_player.advance(block_frames,
                            static_cast<uint32_t>(sample_rate),
                            *voice_manager);

        // Pull with diagnostic tap
        voice_manager->pull_with_tap(buffer, tap.get());

        if (chorus_enabled && chorus) {
            chorus->pull(buffer);
        }

        for (auto& fx : post_chain) {
            fx->pull(buffer);
        }
    });

    }
};

extern "C" {

OscillatorHandle oscillator_create(int type, unsigned int sample_rate) {
    try {
        std::unique_ptr<audio::Processor> processor;
        
        switch (type) {
            case OSC_SINE:
                processor = std::make_unique<audio::SineOscillatorProcessor>(sample_rate);
                break;
            case OSC_SQUARE:
                processor = std::make_unique<audio::SquareOscillatorProcessor>(sample_rate);
                break;
            case OSC_TRIANGLE:
                processor = std::make_unique<audio::TriangleOscillatorProcessor>(sample_rate);
                break;
            case OSC_SAWTOOTH:
                processor = std::make_unique<audio::SawtoothOscillatorProcessor>(sample_rate);
                break;
            case OSC_WAVETABLE_SINE:
            case OSC_WAVETABLE_SAW:
            case OSC_WAVETABLE_SQUARE:
            case OSC_WAVETABLE_TRIANGLE: {
                audio::WaveType wave_type = audio::WaveType::Sine;
                if (type == OSC_WAVETABLE_SAW) wave_type = audio::WaveType::Saw;
                else if (type == OSC_WAVETABLE_SQUARE) wave_type = audio::WaveType::Square;
                else if (type == OSC_WAVETABLE_TRIANGLE) wave_type = audio::WaveType::Triangle;

                processor = std::make_unique<audio::WavetableOscillatorProcessor>(
                    static_cast<double>(sample_rate), 2048, wave_type);
                break;
            }
            default:
                return nullptr;
        }
        
        return static_cast<OscillatorHandle>(new OscillatorHandleImpl(std::move(processor), sample_rate));
    } catch (...) {
        return nullptr;
    }
}

void oscillator_destroy(OscillatorHandle handle) {
    if (handle) {
        delete static_cast<OscillatorHandleImpl*>(handle);
    }
}

int oscillator_set_frequency(OscillatorHandle handle, double freq) {
    if (!handle) return -1;
    auto* impl = static_cast<OscillatorHandleImpl*>(handle);
    try {
        auto* osc_proc = dynamic_cast<audio::OscillatorProcessor*>(impl->processor.get());
        if (osc_proc) {
            osc_proc->set_frequency(freq);
            return 0;
        }
        auto* wavetable = dynamic_cast<audio::WavetableOscillatorProcessor*>(impl->processor.get());
        if (wavetable) {
            wavetable->setFrequency(freq);
            return 0;
        }
        return -1;
    } catch (...) {
        return -1;
    }
}

int oscillator_set_frequency_glide(OscillatorHandle handle, double target_freq, double duration_seconds) {
    if (!handle) return -1;
    auto* impl = static_cast<OscillatorHandleImpl*>(handle);
    try {
        auto* osc_proc = dynamic_cast<audio::OscillatorProcessor*>(impl->processor.get());
        if (osc_proc) {
            osc_proc->set_frequency_glide(target_freq, duration_seconds);
            return 0;
        }
        auto* wavetable = dynamic_cast<audio::WavetableOscillatorProcessor*>(impl->processor.get());
        if (wavetable) {
            wavetable->setFrequencyGlide(target_freq, duration_seconds);
            return 0;
        }
        return -1;
    } catch (...) {
        return -1;
    }
}

int oscillator_process(OscillatorHandle handle, float* output, size_t frames) {
    if (!handle || !output || frames == 0) return -1;
    auto* impl = static_cast<OscillatorHandleImpl*>(handle);
    try {
        std::span<float> output_span(output, frames);
        impl->processor->pull(output_span);
        return 0;
    } catch (...) {
        return -1;
    }
}

int oscillator_reset(OscillatorHandle handle) {
    if (!handle) return -1;
    auto* impl = static_cast<OscillatorHandleImpl*>(handle);
    try {
        impl->processor->reset();
        return 0;
    } catch (...) {
        return -1;
    }
}

int oscillator_get_metrics(OscillatorHandle handle, uint64_t* last_time_ns, uint64_t* max_time_ns, uint64_t* total_blocks) {
    if (!handle || !last_time_ns || !max_time_ns || !total_blocks) return -1;
    auto* impl = static_cast<OscillatorHandleImpl*>(handle);
    try {
        auto metrics = impl->processor->get_metrics();
        *last_time_ns = static_cast<uint64_t>(metrics.last_execution_time.count());
        *max_time_ns = static_cast<uint64_t>(metrics.max_execution_time.count());
        *total_blocks = metrics.total_blocks_processed;
        return 0;
    } catch (...) {
        return -1;
    }
}

int set_osc_wavetype(void* instance, int type) {
    if (!instance) return -1;
    auto* impl = static_cast<OscillatorHandleImpl*>(instance);
    try {
        auto* wavetable = dynamic_cast<audio::WavetableOscillatorProcessor*>(impl->processor.get());
        if (!wavetable) return -1;
        
        audio::WaveType wave_type;
        switch (type) {
            case WAVE_SINE: wave_type = audio::WaveType::Sine; break;
            case WAVE_SAW: wave_type = audio::WaveType::Saw; break;
            case WAVE_SQUARE: wave_type = audio::WaveType::Square; break;
            case WAVE_TRIANGLE: wave_type = audio::WaveType::Triangle; break;
            default: return -1;
        }
        wavetable->setWaveType(wave_type);
        return 0;
    } catch (...) {
        return -1;
    }
}

EnvelopeHandle envelope_create(int type, unsigned int sample_rate) {
    try {
        std::unique_ptr<audio::EnvelopeProcessor> processor;
        switch (type) {
            case ENV_ADSR: processor = std::make_unique<audio::AdsrEnvelopeProcessor>(sample_rate); break;
            case ENV_AD: processor = std::make_unique<audio::ADEnvelopeProcessor>(sample_rate); break;
            default: return nullptr;
        }
        return static_cast<EnvelopeHandle>(new EnvelopeHandleImpl(std::move(processor), sample_rate));
    } catch (...) { return nullptr; }
}

void envelope_destroy(EnvelopeHandle handle) {
    if (handle) delete static_cast<EnvelopeHandleImpl*>(handle);
}

int envelope_gate_on(EnvelopeHandle handle) {
    if (!handle) return -1;
    auto* impl = static_cast<EnvelopeHandleImpl*>(handle);
    try { impl->processor->gate_on(); return 0; } catch (...) { return -1; }
}

int envelope_gate_off(EnvelopeHandle handle) {
    if (!handle) return -1;
    auto* impl = static_cast<EnvelopeHandleImpl*>(handle);
    try { impl->processor->gate_off(); return 0; } catch (...) { return -1; }
}

int envelope_set_adsr(EnvelopeHandle handle, float attack, float decay, float sustain, float release) {
    if (!handle) return -1;
    auto* impl = static_cast<EnvelopeHandleImpl*>(handle);
    try {
        auto* adsr = dynamic_cast<audio::AdsrEnvelopeProcessor*>(impl->processor.get());
        if (adsr) {
            adsr->set_attack_time(attack); adsr->set_decay_time(decay);
            adsr->set_sustain_level(sustain); adsr->set_release_time(release);
            return 0;
        }
        return -1;
    } catch (...) { return -1; }
}

int envelope_set_ad(EnvelopeHandle handle, float attack, float decay) {
    if (!handle) return -1;
    auto* impl = static_cast<EnvelopeHandleImpl*>(handle);
    try {
        auto* ad = dynamic_cast<audio::ADEnvelopeProcessor*>(impl->processor.get());
        if (ad) { ad->set_attack_time(attack); ad->set_decay_time(decay); return 0; }
        return -1;
    } catch (...) { return -1; }
}

int envelope_process(EnvelopeHandle handle, float* output, size_t frames) {
    if (!handle || !output || frames == 0) return -1;
    auto* impl = static_cast<EnvelopeHandleImpl*>(handle);
    try {
        std::span<float> output_span(output, frames);
        impl->processor->pull(output_span);
        return 0;
    } catch (...) { return -1; }
}

int envelope_is_active(EnvelopeHandle handle) {
    if (!handle) return 0;
    auto* impl = static_cast<EnvelopeHandleImpl*>(handle);
    try { return impl->processor->is_active() ? 1 : 0; } catch (...) { return 0; }
}

EngineHandle engine_create(unsigned int sample_rate) {
    audio::register_builtin_processors();
    EngineHandle handle = nullptr;
    try {
        handle = static_cast<EngineHandle>(new EngineHandleImpl(sample_rate));
    } catch (...) { return nullptr; }

    // Bootstrap a default VCO → ENV → VCA chain so the engine produces audio
    // immediately after engine_create() without requiring an explicit engine_bake().
    // Callers that invoke engine_add_module() + engine_bake() will replace this chain.
    engine_add_module(handle, "COMPOSITE_GENERATOR", "VCO");
    engine_add_module(handle, "ADSR_ENVELOPE",       "ENV");
    engine_add_module(handle, "VCA",                 "VCA");
    engine_connect_ports(handle, "ENV", "envelope_out", "VCA", "gain_cv");
    engine_bake(handle);
    set_param(handle, "sine_gain", 1.0f); // default: sine oscillator active
    return handle;
}

void engine_destroy(EngineHandle handle) {
    if (handle) delete static_cast<EngineHandleImpl*>(handle);
}

void engine_note_on(EngineHandle handle, int note, float velocity) {
    if (!handle) return;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    impl->voice_manager->note_on(note, velocity);
}

void engine_note_off(EngineHandle handle, int note) {
    if (!handle) return;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    impl->voice_manager->note_off(note);
}

void engine_set_note_pan(EngineHandle handle, int note, float pan) {
    if (!handle) return;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    impl->voice_manager->set_note_pan(note, pan);
}

int engine_set_adsr(EngineHandle handle, float attack, float decay, float sustain, float release) {
    if (!handle) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    impl->voice_manager->set_parameter_by_name("amp_attack",  attack);
    impl->voice_manager->set_parameter_by_name("amp_decay",   decay);
    impl->voice_manager->set_parameter_by_name("amp_sustain", sustain);
    impl->voice_manager->set_parameter_by_name("amp_release", release);
    return 0;
}

int engine_process(EngineHandle handle, float* output, size_t frames) {
    if (!handle || !output || frames == 0) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    try {
        // Resize scratch buffers if needed (not in the audio thread, so allocation is safe).
        if (impl->process_left.size() < frames) {
            impl->process_left.resize(frames);
            impl->process_right.resize(frames);
        }

        audio::AudioBuffer buffer;
        buffer.left  = std::span<float>(impl->process_left.data(),  frames);
        buffer.right = std::span<float>(impl->process_right.data(), frames);

        // Sample-accurate clock and SMF sequencer advance.
        impl->clock.advance(static_cast<int32_t>(frames));
        impl->midi_player.advance(static_cast<uint32_t>(frames),
                                  static_cast<uint32_t>(impl->sample_rate),
                                  *impl->voice_manager);

        // Stereo pull: applies voice panning via SummingBus, same path as the HAL callback.
        impl->voice_manager->pull_with_tap(buffer, impl->tap.get());

        if (impl->chorus_enabled && impl->chorus) {
            impl->chorus->pull(buffer);
        }

        for (auto& fx : impl->post_chain) {
            fx->pull(buffer);
        }

        // Interleave L/R into the caller's stereo buffer: [L0, R0, L1, R1, ...]
        for (size_t i = 0; i < frames; ++i) {
            output[i * 2]     = buffer.left[i];
            output[i * 2 + 1] = buffer.right[i];
        }
        return 0;
    } catch (...) { return -1; }
}

void engine_process_midi_bytes(EngineHandle handle, const uint8_t* data, size_t size, uint32_t sampleOffset) {
    if (!handle || !data || size == 0) return;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    impl->voice_manager->processMidiBytes(data, size, sampleOffset);
}

int engine_start(EngineHandle handle) {
    if (!handle) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    try {
        return impl->driver->start() ? 0 : -1;
    } catch (...) { return -1; }
}

int engine_stop(EngineHandle handle) {
    if (!handle) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    try {
        impl->driver->stop();
        return 0;
    } catch (...) { return -1; }
}

int engine_set_bpm(EngineHandle handle, double bpm) {
    if (!handle) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    impl->clock.set_bpm(bpm);
    return 0;
}

double engine_get_bpm(EngineHandle handle) {
    if (!handle) return 0.0;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    return impl->clock.bpm();
}

int engine_set_meter(EngineHandle handle, int beats_per_bar) {
    if (!handle) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    impl->clock.set_meter(static_cast<int32_t>(beats_per_bar));
    return 0;
}

int engine_get_musical_time(EngineHandle handle, int* bar, int* beat, int* tick) {
    if (!handle) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    auto time = impl->clock.current_time();
    if (bar) *bar = time.bar;
    if (beat) *beat = time.beat;
    if (tick) *tick = time.tick;
    return 0;
}

int engine_get_total_ticks(EngineHandle handle, int64_t* ticks) {
    if (!handle || !ticks) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    *ticks = impl->clock.total_ticks();
    return 0;
}

int engine_note_on_name(EngineHandle handle, const char* note_name, float velocity) {
    if (!handle || !note_name) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    try {
        audio::Note note(note_name);
        double freq = impl->tuning.get_frequency(note);
        impl->voice_manager->note_on(note.midi_note(), velocity, freq);
        return 0;
    } catch (...) {
        return -1;
    }
}

int engine_note_off_name(EngineHandle handle, const char* note_name) {
    if (!handle || !note_name) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    try {
        audio::Note note(note_name);
        impl->voice_manager->note_off(note.midi_note());
        return 0;
    } catch (...) {
        return -1;
    }
}

void engine_print_graph(EngineHandle handle) {
    if (!handle) return;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    for (auto& slot : impl->voice_manager->get_voices()) {
        if (slot.voice) {
            slot.voice->borrow_buffer(); // Just to access graph implicitly via Voice
        }
    }
    audio::AudioLogger::instance().log_message("GraphPrint", "requested");
}

void engine_flush_logs(EngineHandle /* handle */) {
    audio::AudioLogger::instance().set_log_to_console(true);
    audio::AudioLogger::instance().flush();
}


int engine_set_delay_enabled(EngineHandle /* handle */, int /* enabled */) {
    return 0;
}

int engine_get_xrun_count(EngineHandle handle) {
    if (!handle) return 0;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    return impl->driver->get_xrun_count();
}

/**
 * @brief Load a patch from a JSON file.
 *
 * Supports two formats:
 *   version 1 — legacy parameter-map format (backwards compatible).
 *   version 2 — Phase 15 multi-group format with typed chain + named connections.
 *
 * Version 2 JSON shape (single or multi-group):
 * {
 *   "version": 2,
 *   "name": "...",
 *   "groups": [
 *     {
 *       "id": 0,
 *       "chain": [
 *         {"type": "COMPOSITE_GENERATOR", "tag": "VCO"},
 *         {"type": "ADSR_ENVELOPE",        "tag": "ENV"},
 *         {"type": "VCA",                  "tag": "VCA"}
 *       ],
 *       "connections": [
 *         {"from_tag": "ENV", "from_port": "envelope_out",
 *          "to_tag":   "VCA", "to_port":   "gain_cv"}
 *       ],
 *       "parameters": { "saw_gain": 1.0, "amp_attack": 0.01, ... }
 *     }
 *   ]
 * }
 *
 * Multi-group patches apply the chain from group 0 to all voices (Phase 15
 * uses a single global topology; per-group topologies are Phase 15.5+).
 */
int engine_load_patch(EngineHandle handle, const char* path) {
    if (!handle || !path) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    auto& log = audio::AudioLogger::instance();

    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            log.log_message("engine_load_patch", ("Failed to open: " + std::string(path)).c_str());
            return -1;
        }
        nlohmann::json j;
        file >> j;

        int version = j.value("version", 1);

        if (version >= 2) {
            // --- Patch v2: typed chain + named connections ---
            if (!j.contains("groups") || !j["groups"].is_array()
                    || j["groups"].empty()) {
                log.log_message("engine_load_patch", "v2 patch missing 'groups' array");
                return -1;
            }
            // Phase 15: apply the first group's chain to all voices.
            const auto& group = j["groups"][0];

            // Build chain
            impl->pending_modules.clear();
            impl->pending_connections.clear();

            if (group.contains("chain")) {
                for (const auto& m : group["chain"]) {
                    std::string type = m.at("type").get<std::string>();
                    std::string tag  = m.at("tag").get<std::string>();
                    if (engine_add_module(handle, type.c_str(), tag.c_str()) != 0) {
                        log.log_message("engine_load_patch",
                            ("Unknown module type: " + type).c_str());
                        return -1;
                    }
                }
            }
            if (group.contains("connections")) {
                for (const auto& c : group["connections"]) {
                    engine_connect_ports(handle,
                        c.at("from_tag").get<std::string>().c_str(),
                        c.at("from_port").get<std::string>().c_str(),
                        c.at("to_tag").get<std::string>().c_str(),
                        c.at("to_port").get<std::string>().c_str());
                }
            }
            if (engine_bake(handle) != 0) {
                log.log_message("engine_load_patch", "engine_bake() failed");
                return -1;
            }
            // Apply parameters after bake (voices now exist with the new chain).
            // Supports two formats:
            //   v2 tag-keyed: {"VCO": {"saw_gain": 1.0}, "ENV": {"attack": 0.01}}
            //   flat fallback: {"saw_gain": 1.0, "attack": 0.01}  (legacy / v1 compat)
            if (group.contains("parameters")) {
                for (const auto& [key, val] : group["parameters"].items()) {
                    if (val.is_object()) {
                        // Tag-keyed: key is a module tag, val is {param_name: value}
                        for (const auto& [param_name, param_val] : val.items()) {
                            impl->voice_manager->set_tag_parameter(key, param_name,
                                static_cast<float>(param_val.get<double>()));
                        }
                    } else if (val.is_number()) {
                        // Flat fallback: key is a param name, val is the value
                        impl->voice_manager->set_parameter_by_name(key, val.get<float>());
                    }
                }
            }
            log.log_message("engine_load_patch", ("Loaded v2: " + j.value("name", "")).c_str());
            return 0;

        } else {
            log.log_message("engine_load_patch", "v1 patch format is not supported — use v2");
            return -1;
        }
    } catch (const std::exception& e) {
        log.log_message("engine_load_patch", e.what());
        return -1;
    }
}

// ---------------------------------------------------------------------------
// Phase 20: Host Device Enumeration — delegates entirely to the HAL.
// No platform ifdefs here; all platform knowledge lives in the HAL layer.
// ---------------------------------------------------------------------------

int host_get_device_count() {
    const auto devices = hal::AudioDriver::enumerate_devices();
    return static_cast<int>(devices.size());
}

int host_get_device_name(int index, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0 || index < 0) return -1;
    const auto devices = hal::AudioDriver::enumerate_devices();
    if (static_cast<size_t>(index) >= devices.size()) return -1;
    std::strncpy(buffer, devices[static_cast<size_t>(index)].name.c_str(), buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return 0;
}

int host_get_device_sample_rate(int index) {
    if (index < 0) return -1;
    const auto devices = hal::AudioDriver::enumerate_devices();
    if (static_cast<size_t>(index) >= devices.size()) return -1;
    return devices[static_cast<size_t>(index)].default_sample_rate;
}

int host_get_device_block_size(int index) {
    if (index < 0) return -1;
    const auto devices = hal::AudioDriver::enumerate_devices();
    if (static_cast<size_t>(index) >= devices.size()) return -1;
    return devices[static_cast<size_t>(index)].default_block_size;
}

int host_get_supported_sample_rates(int index, int* out_rates, int max_count) {
    if (index < 0 || !out_rates || max_count <= 0) return -1;
    const auto devices = hal::AudioDriver::enumerate_devices();
    if (static_cast<size_t>(index) >= devices.size()) return -1;
    const auto& rates = devices[static_cast<size_t>(index)].supported_sample_rates;
    const int n = static_cast<int>(std::min(rates.size(), static_cast<size_t>(max_count)));
    for (int i = 0; i < n; ++i) out_rates[i] = rates[static_cast<size_t>(i)];
    return n;
}

int host_get_supported_block_sizes(int index, int* out_sizes, int max_count) {
    if (index < 0 || !out_sizes || max_count <= 0) return -1;
    const auto devices = hal::AudioDriver::enumerate_devices();
    if (static_cast<size_t>(index) >= devices.size()) return -1;
    const auto& sizes = devices[static_cast<size_t>(index)].supported_block_sizes;
    const int n = static_cast<int>(std::min(sizes.size(), static_cast<size_t>(max_count)));
    for (int i = 0; i < n; ++i) out_sizes[i] = sizes[static_cast<size_t>(i)];
    return n;
}

int engine_get_driver_sample_rate(EngineHandle handle) {
    if (!handle) return -1;
    return static_cast<EngineHandleImpl*>(handle)->driver->sample_rate();
}

int engine_get_driver_block_size(EngineHandle handle) {
    if (!handle) return -1;
    return static_cast<EngineHandleImpl*>(handle)->driver->block_size();
}

int engine_get_driver_name(EngineHandle handle, char* buffer, size_t buffer_size) {
    if (!handle || !buffer || buffer_size == 0) return -1;
    const std::string name = static_cast<EngineHandleImpl*>(handle)->driver->device_name();
    std::strncpy(buffer, name.c_str(), buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return 0;
}

int set_param(void* handle, const char* name, float value) {
    if (!handle || !name) return -1;
    try {
        auto* base = static_cast<HandleBase*>(handle);
        
        if (base->type == HandleType::Engine) {
            auto* engine = static_cast<EngineHandleImpl*>(handle);
            engine->voice_manager->set_parameter_by_name(name, value);
            return 0;
        }

        if (base->type == HandleType::Envelope) {
            auto* env_impl = static_cast<EnvelopeHandleImpl*>(handle);
            auto* adsr = dynamic_cast<audio::AdsrEnvelopeProcessor*>(env_impl->processor.get());
            if (adsr) {
                if (std::strcmp(name, "attack") == 0) { adsr->set_attack_time(value); return 0; }
                if (std::strcmp(name, "decay") == 0) { adsr->set_decay_time(value); return 0; }
                if (std::strcmp(name, "sustain") == 0) { adsr->set_sustain_level(value); return 0; }
                if (std::strcmp(name, "release") == 0) { adsr->set_release_time(value); return 0; }
            }
        }

        return -1;
    } catch (...) { return -1; }
}

int engine_audiotap_reset(EngineHandle handle) {
    if (!handle) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    impl->tap->reset();
    return 0;
}

int engine_audiotap_read(EngineHandle handle, float* buffer, size_t frames) {
    if (!handle || !buffer || frames == 0) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    std::vector<float> data(frames);
    impl->tap->read(data);
    std::memcpy(buffer, data.data(), frames * sizeof(float));
    return 0;
}

int engine_set_chorus_mode(EngineHandle handle, int mode) {
    if (!handle) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    audio::JunoChorus::Mode m;
    switch (mode) {
        case 1: m = audio::JunoChorus::Mode::I; break;
        case 2: m = audio::JunoChorus::Mode::II; break;
        case 3: m = audio::JunoChorus::Mode::I_II; break;
        default: m = audio::JunoChorus::Mode::Off; break;
    }
    impl->chorus->set_mode(m);
    return 0;
}

int engine_set_chorus_enabled(EngineHandle handle, int enabled) {
    if (!handle) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    impl->chorus_enabled = (enabled != 0);
    return 0;
}

void audio_log_message(const char* tag, const char* message) {
    audio::AudioLogger::instance().log_message(tag, message);
}

void audio_log_event(const char* tag, float value) {
    audio::AudioLogger::instance().log_event(tag, value);
}

void audio_engine_init() {}
void audio_engine_cleanup() {}

// ---------------------------------------------------------------------------
// Phase 15: Module Registry Query
// ---------------------------------------------------------------------------

int engine_get_module_count(EngineHandle handle) {
    if (!handle) return -1;
    auto names = audio::ModuleRegistry::instance().type_names();
    return static_cast<int>(names.size());
}

int engine_get_module_type(EngineHandle handle, int index,
                           char* buffer, size_t buf_size) {
    if (!handle || !buffer || buf_size == 0) return -1;
    auto names = audio::ModuleRegistry::instance().type_names();
    if (index < 0 || static_cast<size_t>(index) >= names.size()) return -1;
    std::snprintf(buffer, buf_size, "%s", names[static_cast<size_t>(index)].c_str());
    return 0;
}

// ---------------------------------------------------------------------------
// Phase 15: Chain Construction
// ---------------------------------------------------------------------------

int engine_add_module(EngineHandle handle,
                      const char* type_name,
                      const char* tag) {
    if (!handle || !type_name || !tag) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    if (!audio::ModuleRegistry::instance().find(type_name)) return -1;
    impl->pending_modules.push_back({type_name, tag});
    return 0;
}

int engine_connect_ports(EngineHandle handle,
                         const char* from_tag, const char* from_port,
                         const char* to_tag,   const char* to_port) {
    if (!handle || !from_tag || !from_port || !to_tag || !to_port) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    impl->pending_connections.push_back({from_tag, from_port, to_tag, to_port});
    return 0;
}

int engine_bake(EngineHandle handle) {
    if (!handle) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);

    if (impl->pending_modules.empty()) return -1; // nothing to bake

    const int sr = impl->sample_rate;
    auto& reg = audio::ModuleRegistry::instance();

    // Snapshot so the lambda captures by value.
    auto modules     = impl->pending_modules;
    auto connections = impl->pending_connections;

    try {
        impl->voice_manager->rebuild_all_voices([&]() {
            auto voice = std::make_unique<audio::Voice>(sr);
            for (const auto& m : modules) {
                auto proc = reg.create(m.type_name, sr);
                if (!proc) throw std::runtime_error(
                    "engine_bake: unknown type '" + m.type_name + "'");
                voice->add_processor(std::move(proc), m.tag);
            }
            for (const auto& c : connections) {
                voice->connect(c.from_tag, c.from_port,
                               c.to_tag,   c.to_port);
            }
            voice->bake();
            return voice;
        });
    } catch (const std::exception& e) {
        audio::AudioLogger::instance().log_message("engine_bake", e.what());
        return -1;
    }

    // Clear pending spec so a new chain can be described after this.
    impl->pending_modules.clear();
    impl->pending_connections.clear();
    return 0;
}

// ---------------------------------------------------------------------------
// Phase 22A: SMF File Playback
// ---------------------------------------------------------------------------

int engine_load_midi(EngineHandle handle, const char* path) {
    if (!handle || !path) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    try {
        auto data = audio::SmfParser::load(path);
        impl->midi_player.load(std::move(data));
        return 0;
    } catch (const std::exception& e) {
        audio::AudioLogger::instance().log_message("engine_load_midi", e.what());
        return -1;
    }
}

int engine_midi_play(EngineHandle handle) {
    if (!handle) return -1;
    static_cast<EngineHandleImpl*>(handle)->midi_player.play();
    return 0;
}

int engine_midi_stop(EngineHandle handle) {
    if (!handle) return -1;
    static_cast<EngineHandleImpl*>(handle)->midi_player.stop();
    return 0;
}

int engine_midi_rewind(EngineHandle handle) {
    if (!handle) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    impl->midi_player.stop();
    impl->midi_player.rewind();
    return 0;
}

int engine_midi_get_position(EngineHandle handle, uint64_t* tick) {
    if (!handle || !tick) return -1;
    *tick = static_cast<EngineHandleImpl*>(handle)->midi_player.position_ticks();
    return 0;
}

// ---------------------------------------------------------------------------
// Phase 19: Global Post-Processing Chain
// ---------------------------------------------------------------------------

int engine_post_chain_push(EngineHandle handle, const char* type_name) {
    if (!handle || !type_name) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    const int sr = impl->sample_rate;

    std::unique_ptr<audio::Processor> fx;
    const std::string t(type_name);
    if      (t == "REVERB_FREEVERB") fx = std::make_unique<audio::FreeverbProcessor>(sr);
    else if (t == "REVERB_FDN")      fx = std::make_unique<audio::FdnReverbProcessor>(sr);
    else if (t == "PHASER")          fx = std::make_unique<audio::PhaserProcessor>(sr);
    else return -1;

    impl->post_chain.push_back(std::move(fx));
    return static_cast<int>(impl->post_chain.size()) - 1;
}

int engine_post_chain_set_param(EngineHandle handle, int fx_index,
                                const char* name, float value) {
    if (!handle || !name || fx_index < 0) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    if (static_cast<size_t>(fx_index) >= impl->post_chain.size()) return -1;
    return impl->post_chain[static_cast<size_t>(fx_index)]->apply_parameter(name, value) ? 0 : -1;
}

int engine_post_chain_clear(EngineHandle handle) {
    if (!handle) return -1;
    static_cast<EngineHandleImpl*>(handle)->post_chain.clear();
    return 0;
}

} // extern "C"
