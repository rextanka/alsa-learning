/**
 * @file ProcessorRegistrations.cpp
 * @brief Built-in module type registrations for ModuleRegistry (Phase 15).
 *
 * register_builtin_processors() registers all built-in processors into the
 * ModuleRegistry singleton. It is idempotent — subsequent calls are no-ops
 * because register_module() overwrites the same key.
 *
 * Call from engine_create() (AudioBridge.cpp) and from unit tests that need
 * the registry populated. A static local ensures the registrations run at
 * most once even if called from multiple threads at startup.
 *
 * To add a new module: include its header, add a register_module() call below.
 */

#include "ModuleRegistry.hpp"
#include "../dsp/routing/CompositeGenerator.hpp"
#include "../dsp/envelope/AdsrEnvelopeProcessor.hpp"
#include "../dsp/VcaProcessor.hpp"
#include "../dsp/filter/MoogLadderProcessor.hpp"
#include "../dsp/filter/DiodeLadderProcessor.hpp"
#include "../dsp/oscillator/LfoProcessor.hpp"
#include "../dsp/oscillator/WhiteNoiseProcessor.hpp"
#include "../dsp/fx/JunoChorus.hpp"

namespace audio {

void register_builtin_processors() {
    auto& reg = ModuleRegistry::instance();

    reg.register_module(
        "COMPOSITE_GENERATOR",
        "Multi-waveform VCO: sawtooth, pulse, sub, sine, triangle, wavetable, noise",
        [](int sr) { return std::make_unique<CompositeGenerator>(sr); }
    );
    reg.register_module(
        "ADSR_ENVELOPE",
        "4-stage ADSR envelope generator (exponential IIR curves)",
        [](int sr) { return std::make_unique<AdsrEnvelopeProcessor>(sr); }
    );
    reg.register_module(
        "VCA",
        "Voltage-controlled amplifier — scales audio by a gain CV signal",
        [](int /*sr*/) { return std::make_unique<VcaProcessor>(); }
    );
    reg.register_module(
        "MOOG_FILTER",
        "4-pole Moog transistor ladder low-pass filter",
        [](int sr) { return std::make_unique<MoogLadderProcessor>(sr); }
    );
    reg.register_module(
        "DIODE_FILTER",
        "TB-303 style diode ladder filter with non-linear saturation",
        [](int sr) { return std::make_unique<DiodeLadderProcessor>(sr); }
    );
    reg.register_module(
        "LFO",
        "Low-frequency oscillator (sine/triangle/square/saw) with intensity smoothing",
        [](int sr) { return std::make_unique<LfoProcessor>(sr); }
    );
    reg.register_module(
        "WHITE_NOISE",
        "LCG-based white noise generator (PORT_AUDIO, range [-1, 1])",
        [](int /*sr*/) { return std::make_unique<WhiteNoiseProcessor>(); }
    );
    reg.register_module(
        "JUNO_CHORUS",
        "Dual-rate BBD chorus emulating the Juno-60 stereo width effect",
        [](int sr) { return std::make_unique<JunoChorus>(sr); }
    );
}

} // namespace audio
