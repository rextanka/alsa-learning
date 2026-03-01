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
#include "LfoProcessor.hpp"
#include "AdsrEnvelopeProcessor.hpp"
#include "ADEnvelopeProcessor.hpp"
#include "MoogLadderProcessor.hpp"
#include "DiodeLadderProcessor.hpp"
#include "DelayLine.hpp"
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
#include "PatchStore.hpp"
#include <memory>
#include <span>
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
    audio::MusicalClock clock;
    audio::TwelveToneEqual tuning;
    std::unordered_map<std::string, int> param_name_to_id;
    int sample_rate;
    int next_processor_id = 100; // Start at 100 to avoid confusion with voice indices

    EngineHandleImpl(int sr)
        : HandleBase(HandleType::Engine)
        , voice_manager(std::make_unique<audio::VoiceManager>(sr))
        , clock(static_cast<double>(sr))
        , sample_rate(sr)
    {
#ifdef __APPLE__
        driver = std::make_unique<hal::CoreAudioDriver>(sr, 512);
#else
        driver = std::make_unique<hal::AlsaDriver>(sr, 512);
#endif
        // Link the driver to the voice manager
        driver->set_stereo_callback([this](audio::AudioBuffer& buffer) {
            voice_manager->pull(buffer);
        });

        // Initialize parameter mapping for fast UI reflection
        param_name_to_id["osc_pw"] = 10;
        param_name_to_id["sub_gain"] = 11;
        param_name_to_id["saw_gain"] = 12;
        param_name_to_id["pulse_gain"] = 13;
        param_name_to_id["vcf_cutoff"] = 1;
        param_name_to_id["vcf_res"] = 2;
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
    try { return static_cast<EngineHandle>(new EngineHandleImpl(sample_rate)); } catch (...) { return nullptr; }
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
    for (auto& slot : impl->voice_manager->get_voices()) {
        if (auto* adsr = dynamic_cast<audio::AdsrEnvelopeProcessor*>(&slot.voice->envelope())) {
            adsr->set_attack_time(attack);
            adsr->set_decay_time(decay);
            adsr->set_sustain_level(sustain);
            adsr->set_release_time(release);
        }
    }
    return 0;
}

int engine_process(EngineHandle handle, float* output, size_t frames) {
    if (!handle || !output || frames == 0) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    try {
        std::span<float> output_span(output, frames);
        
        // Sample-accurate clock advance
        impl->clock.advance(static_cast<int32_t>(frames));
        
        impl->voice_manager->pull(output_span);
        return 0;
    } catch (...) { return -1; }
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

void engine_process_midi_bytes(EngineHandle handle, const uint8_t* data, size_t size, uint32_t sampleOffset) {
    if (!handle || !data || size == 0) return;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    impl->voice_manager->processMidiBytes(data, size, sampleOffset);
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

int engine_set_filter_type(EngineHandle handle, int /* type */) {
    return 0; // Interface presence
}

int engine_set_delay_enabled(EngineHandle handle, int /* enabled */) {
    return 0;
}

int engine_set_modulation(EngineHandle handle, int source, int target, float intensity) {
    if (!handle) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    
    // Apply to all voices for now
    for (auto& slot : impl->voice_manager->get_voices()) {
        if (slot.voice) {
            slot.voice->matrix().set_connection(
                static_cast<audio::ModulationSource>(source),
                static_cast<audio::ModulationTarget>(target),
                intensity
            );
        }
    }
    return 0;
}

int engine_clear_modulations(EngineHandle handle) {
    if (!handle) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    for (auto& slot : impl->voice_manager->get_voices()) {
        if (slot.voice) {
            slot.voice->matrix().clear_all();
        }
    }
    return 0;
}

int engine_save_patch(EngineHandle handle, const char* path) {
    if (!handle || !path) return -1;
    // Implementation of saving from current engine state...
    return 0;
}

int engine_load_patch(EngineHandle handle, const char* path) {
    if (!handle || !path) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    audio::PatchData patch;
    if (audio::PatchStore::load_from_file(patch, path)) {
        // Apply parameters
        for (auto const& [name, value] : patch.parameters) {
            impl->voice_manager->set_parameter_by_name(name, value);
        }

        // Apply ADSR specific (if in parameters)
        float a = patch.parameters.count("attack") ? patch.parameters.at("attack") : 0.01f;
        float d = patch.parameters.count("decay") ? patch.parameters.at("decay") : 0.1f;
        float s = patch.parameters.count("sustain") ? patch.parameters.at("sustain") : 0.5f;
        float r = patch.parameters.count("release") ? patch.parameters.at("release") : 0.1f;
        engine_set_adsr(handle, a, d, s, r);

        // Apply modulations
        engine_clear_modulations(handle);
        for (auto const& conn : patch.modulations) {
            engine_set_modulation(handle, static_cast<int>(conn.source), static_cast<int>(conn.target), conn.intensity);
        }
        
        return 0;
    }
    return -1;
}

int host_get_device_count() {
#ifdef __APPLE__
    // Minimal mock for now until full CoreAudio enumeration is implemented
    return 1; 
#else
    return 0;
#endif
}

int host_get_device_name(int index, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return -1;
    if (index != 0) return -1;

#ifdef __APPLE__
    // UTF-8 check: Hardcoded name with potential Unicode
    const char* name = "Default Output Device";
    std::strncpy(buffer, name, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return 0;
#else
    return -1;
#endif
}

int host_get_device_sample_rate(int index) {
    if (index != 0) return 0;
#ifdef __APPLE__
    return 44100; // Mock until linked to real driver query
#else
    return 0;
#endif
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
            auto* ad = dynamic_cast<audio::ADEnvelopeProcessor*>(env_impl->processor.get());
            if (ad) {
                if (std::strcmp(name, "attack") == 0) { ad->set_attack_time(value); return 0; }
                if (std::strcmp(name, "decay") == 0) { ad->set_decay_time(value); return 0; }
            }
        }

        if (base->type == HandleType::Oscillator) {
             auto* osc_impl = static_cast<OscillatorHandleImpl*>(handle);
             // Handled frequency etc via specific functions but could add more here
        }

        // Generic processors (if we decide to wrap them in HandleBase too)
        // For now, if it's not one of our known handles, we might still be dealing with a raw Processor* 
        // from some legacy tests, but we should transition them.
        
        return -1;
    } catch (...) { return -1; }
}

void audio_log_message(const char* tag, const char* message) {
    audio::AudioLogger::instance().log_message(tag, message);
}

void audio_log_event(const char* tag, float value) {
    audio::AudioLogger::instance().log_event(tag, value);
}

int engine_create_processor(EngineHandle handle, int type) {
    if (!handle) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    
    int id = impl->next_processor_id++;
    std::shared_ptr<audio::Processor> processor;

    switch (type) {
        case PROC_LFO:
            processor = std::make_shared<audio::LfoProcessor>(impl->sample_rate);
            break;
        case PROC_OSCILLATOR:
            processor = std::make_shared<audio::SineOscillatorProcessor>(impl->sample_rate);
            break;
        case PROC_FILTER:
            processor = std::make_shared<audio::MoogLadderProcessor>(impl->sample_rate);
            break;
        default:
            return -1;
    }

    impl->voice_manager->set_mod_source(id, processor);
    return id;
}

int engine_connect_mod(EngineHandle handle, int source_id, int target_id, int param, float intensity) {
    if (!handle) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    impl->voice_manager->add_connection(source_id, target_id, param, intensity);
    return 0;
}

int engine_get_modulation_report(EngineHandle handle, char* buffer, size_t buffer_size) {
    if (!handle || !buffer || buffer_size == 0) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    
    std::string report = "Modulation Report:\n";
    report += "------------------\n";
    
    auto& conns = impl->voice_manager->get_connections();
    for (const auto& c : conns) {
        report += "Src: " + std::to_string(c.source_id);
        report += " -> Tgt: " + std::to_string(c.target_id);
        report += " (Param: " + std::to_string(c.param);
        report += ") @ " + std::to_string(c.intensity) + "\n";
    }
    
    if (conns.empty()) {
        report += "No active connections.\n";
    }
    
    std::strncpy(buffer, report.c_str(), buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return 0;
}

void audio_engine_init() {}
void audio_engine_cleanup() {}

} // extern "C"
