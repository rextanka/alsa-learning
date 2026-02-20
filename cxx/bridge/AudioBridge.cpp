/**
 * @file AudioBridge.cpp
 * @brief C-compatible API bridge for oscillator processors.
 * 
 * This file follows the project rules defined in .cursorrules:
 * - Interoperability: Core C++ logic wrapped in a C-compatible API.
 * - Modern C++: Target C++20/23 for all new code.
 * - C API: Minimal, stable, free of C++ types in public interface.
 */

#include "CInterface.h"
#include "../audio/oscillator/OscillatorProcessor.hpp"
#include "../audio/oscillator/SineOscillatorProcessor.hpp"
#include "../audio/oscillator/SquareOscillatorProcessor.hpp"
#include "../audio/oscillator/SawtoothOscillatorProcessor.hpp"
#include "../audio/oscillator/TriangleOscillatorProcessor.hpp"
#include "../audio/oscillator/WavetableOscillatorProcessor.hpp"
#include "../audio/envelope/AdsrEnvelopeProcessor.hpp"
#include "../audio/VoiceManager.hpp"
#include <memory>
#include <span>

// Internal handle structure (hidden from C API)
struct OscillatorHandleImpl {
    std::unique_ptr<audio::Processor> processor;  // Base pointer to support both OscillatorProcessor and WavetableOscillatorProcessor
    int sample_rate;
    
    OscillatorHandleImpl(std::unique_ptr<audio::Processor> proc, int sr)
        : processor(std::move(proc))
        , sample_rate(sr)
    {
    }
};

struct EnvelopeHandleImpl {
    std::unique_ptr<audio::EnvelopeProcessor> processor;
    int sample_rate;

    EnvelopeHandleImpl(std::unique_ptr<audio::EnvelopeProcessor> proc, int sr)
        : processor(std::move(proc))
        , sample_rate(sr)
    {
    }
};

// Internal bridge handle for the entire engine state
struct EngineHandleImpl {
    std::unique_ptr<audio::VoiceManager> voice_manager;
    int sample_rate;

    EngineHandleImpl(int sr)
        : voice_manager(std::make_unique<audio::VoiceManager>(sr))
        , sample_rate(sr)
    {
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
            case OSC_WAVETABLE_SINE:  // OSC_WAVETABLE (4) is same value, backward compat
            case OSC_WAVETABLE_SAW:
            case OSC_WAVETABLE_SQUARE:
            case OSC_WAVETABLE_TRIANGLE: {
                // Use consolidated WavetableOscillatorProcessor class (with glide support)
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
    if (!handle) {
        return -1;
    }
    
    auto* impl = static_cast<OscillatorHandleImpl*>(handle);
    if (!impl->processor) {
        return -1;
    }
    
    try {
        // Try OscillatorProcessor interface first
        auto* osc_proc = dynamic_cast<audio::OscillatorProcessor*>(impl->processor.get());
        if (osc_proc) {
            osc_proc->set_frequency(freq);
            return 0;
        }
        
        // Try WavetableOscillatorProcessor interface
        auto* wavetable = dynamic_cast<audio::WavetableOscillatorProcessor*>(impl->processor.get());
        if (wavetable) {
            wavetable->setFrequency(freq);
            return 0;
        }
        
        return -1;  // Unknown processor type
    } catch (...) {
        return -1;
    }
}

int oscillator_set_frequency_glide(OscillatorHandle handle, double target_freq, double duration_seconds) {
    if (!handle) {
        return -1;
    }
    
    auto* impl = static_cast<OscillatorHandleImpl*>(handle);
    if (!impl->processor) {
        return -1;
    }
    
    try {
        // OscillatorProcessor supports frequency glide
        auto* osc_proc = dynamic_cast<audio::OscillatorProcessor*>(impl->processor.get());
        if (osc_proc) {
            osc_proc->set_frequency_glide(target_freq, duration_seconds);
            return 0;
        }
        
        // WavetableOscillatorProcessor also supports glide
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
    if (!handle || !output || frames == 0) {
        return -1;
    }
    
    auto* impl = static_cast<OscillatorHandleImpl*>(handle);
    if (!impl->processor) {
        return -1;
    }
    
    try {
        std::span<float> output_span(output, frames);
        impl->processor->pull(output_span);
        return 0;
    } catch (...) {
        return -1;
    }
}

int oscillator_reset(OscillatorHandle handle) {
    if (!handle) {
        return -1;
    }
    
    auto* impl = static_cast<OscillatorHandleImpl*>(handle);
    if (!impl->processor) {
        return -1;
    }
    
    try {
        impl->processor->reset();
        return 0;
    } catch (...) {
        return -1;
    }
}

int oscillator_get_metrics(OscillatorHandle handle, 
                           uint64_t* last_time_ns,
                           uint64_t* max_time_ns,
                           uint64_t* total_blocks) {
    if (!handle || !last_time_ns || !max_time_ns || !total_blocks) {
        return -1;
    }
    
    auto* impl = static_cast<OscillatorHandleImpl*>(handle);
    if (!impl->processor) {
        return -1;
    }
    
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
    if (!instance) {
        return -1;
    }
    
    auto* impl = static_cast<OscillatorHandleImpl*>(instance);
    if (!impl->processor) {
        return -1;
    }
    
    try {
        auto* wavetable = dynamic_cast<audio::WavetableOscillatorProcessor*>(impl->processor.get());
        if (!wavetable) {
            return -1;  // Not a WavetableOscillatorProcessor
        }
        
        audio::WaveType wave_type;
        switch (type) {
            case WAVE_SINE:
                wave_type = audio::WaveType::Sine;
                break;
            case WAVE_SAW:
                wave_type = audio::WaveType::Saw;
                break;
            case WAVE_SQUARE:
                wave_type = audio::WaveType::Square;
                break;
            case WAVE_TRIANGLE:
                wave_type = audio::WaveType::Triangle;
                break;
            default:
                return -1;
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
            case ENV_ADSR:
                processor = std::make_unique<audio::AdsrEnvelopeProcessor>(sample_rate);
                break;
            default:
                return nullptr;
        }
        
        return static_cast<EnvelopeHandle>(new EnvelopeHandleImpl(std::move(processor), sample_rate));
    } catch (...) {
        return nullptr;
    }
}

void envelope_destroy(EnvelopeHandle handle) {
    if (handle) {
        delete static_cast<EnvelopeHandleImpl*>(handle);
    }
}

int envelope_gate_on(EnvelopeHandle handle) {
    if (!handle) return -1;
    auto* impl = static_cast<EnvelopeHandleImpl*>(handle);
    try {
        impl->processor->gate_on();
        return 0;
    } catch (...) {
        return -1;
    }
}

int envelope_gate_off(EnvelopeHandle handle) {
    if (!handle) return -1;
    auto* impl = static_cast<EnvelopeHandleImpl*>(handle);
    try {
        impl->processor->gate_off();
        return 0;
    } catch (...) {
        return -1;
    }
}

int envelope_set_adsr(EnvelopeHandle handle, float attack, float decay, float sustain, float release) {
    if (!handle) return -1;
    auto* impl = static_cast<EnvelopeHandleImpl*>(handle);
    try {
        auto* adsr = dynamic_cast<audio::AdsrEnvelopeProcessor*>(impl->processor.get());
        if (adsr) {
            adsr->set_attack_time(attack);
            adsr->set_decay_time(decay);
            adsr->set_sustain_level(sustain);
            adsr->set_release_time(release);
            return 0;
        }
        return -1;
    } catch (...) {
        return -1;
    }
}

int envelope_process(EnvelopeHandle handle, float* output, size_t frames) {
    if (!handle || !output || frames == 0) return -1;
    auto* impl = static_cast<EnvelopeHandleImpl*>(handle);
    try {
        std::span<float> output_span(output, frames);
        impl->processor->pull(output_span);
        return 0;
    } catch (...) {
        return -1;
    }
}

int envelope_is_active(EnvelopeHandle handle) {
    if (!handle) return 0;
    auto* impl = static_cast<EnvelopeHandleImpl*>(handle);
    try {
        return impl->processor->is_active() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

EngineHandle engine_create(unsigned int sample_rate) {
    try {
        return static_cast<EngineHandle>(new EngineHandleImpl(sample_rate));
    } catch (...) {
        return nullptr;
    }
}

void engine_destroy(EngineHandle handle) {
    if (handle) {
        delete static_cast<EngineHandleImpl*>(handle);
    }
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

int engine_process(EngineHandle handle, float* output, size_t frames) {
    if (!handle || !output || frames == 0) return -1;
    auto* impl = static_cast<EngineHandleImpl*>(handle);
    try {
        std::span<float> output_span(output, frames);
        impl->voice_manager->pull(output_span);
        return 0;
    } catch (...) {
        return -1;
    }
}

} // extern "C"
