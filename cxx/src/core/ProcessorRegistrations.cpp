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
#include "../dsp/filter/CemFilterProcessor.hpp"
#include "../dsp/filter/Ms20FilterProcessor.hpp"
#include "../dsp/oscillator/LfoProcessor.hpp"
#include "../dsp/oscillator/WhiteNoiseProcessor.hpp"
#include "../dsp/fx/JunoChorus.hpp"
#include "../dsp/fx/EchoDelayProcessor.hpp"
#include "../dsp/routing/DrawbarOrganProcessor.hpp"
#include "../dsp/routing/InverterProcessor.hpp"

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
        "4-pole Moog transistor ladder LP (24 dB/oct) — smooth, creamy, thick",
        [](int sr) { return std::make_unique<MoogLadderProcessor>(sr); }
    );
    reg.register_module(
        "DIODE_FILTER",
        "TB-style diode ladder LP — 3/4-pole blend gives 18–24 dB/oct rubbery acid character",
        [](int sr) { return std::make_unique<DiodeLadderProcessor>(sr); }
    );
    reg.register_module(
        "SH_FILTER",
        "SH-style / CEM / IR3109 4-pole ladder LP (24 dB/oct) — clean, liquid, resonant",
        [](int sr) { return std::make_unique<CemFilterProcessor>(sr); }
    );
    reg.register_module(
        "MS20_FILTER",
        "MS-style dual 2-pole HP+LP SVF (12 dB/oct each) — aggressive, gritty, screaming",
        [](int sr) { return std::make_unique<Ms20FilterProcessor>(sr); }
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
    reg.register_module(
        "DRAWBAR_ORGAN",
        "9-partial tonewheel organ at Hammond footage ratios (16' to 1')",
        [](int sr) { return std::make_unique<DrawbarOrganProcessor>(sr); }
    );
    reg.register_module(
        "ECHO_DELAY",
        "Modulated delay line (BBD-style): static delay + LFO shimmer",
        [](int sr) { return std::make_unique<EchoDelayProcessor>(sr); }
    );
    reg.register_module(
        "INVERTER",
        "CV signal inverter/scaler: cv_out = scale * cv_in",
        [](int /*sr*/) { return std::make_unique<InverterProcessor>(); }
    );
}

} // namespace audio
