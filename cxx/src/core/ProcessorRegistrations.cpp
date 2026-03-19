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
#include "../dsp/envelope/ADEnvelopeProcessor.hpp"
#include "../dsp/VcaProcessor.hpp"
#include "../dsp/filter/MoogLadderProcessor.hpp"
#include "../dsp/filter/DiodeLadderProcessor.hpp"
#include "../dsp/filter/CemFilterProcessor.hpp"
#include "../dsp/filter/Ms20FilterProcessor.hpp"
#include "../dsp/filter/HighPassFilterProcessor.hpp"
#include "../dsp/filter/BandPassFilterProcessor.hpp"
#include "../dsp/oscillator/LfoProcessor.hpp"
#include "../dsp/oscillator/WhiteNoiseProcessor.hpp"
#include "../dsp/fx/EchoDelayProcessor.hpp"
#include "../dsp/routing/DrawbarOrganProcessor.hpp"
#include "../dsp/routing/InverterProcessor.hpp"
#include "../dsp/routing/CvMixerProcessor.hpp"
#include "../dsp/routing/CvSplitterProcessor.hpp"
#include "../dsp/routing/MathsProcessor.hpp"
#include "../dsp/routing/GateDelayProcessor.hpp"
#include "../dsp/routing/SampleHoldProcessor.hpp"
#include "../dsp/routing/RingModProcessor.hpp"
#include "../dsp/routing/AudioSplitterProcessor.hpp"
#include "../dsp/routing/AudioMixerProcessor.hpp"
#include "../dsp/dynamics/NoiseGateProcessor.hpp"
#include "../dsp/dynamics/EnvelopeFollowerProcessor.hpp"
#include "../dsp/fx/FreeverbProcessor.hpp"
#include "../dsp/fx/FdnReverbProcessor.hpp"
#include "../dsp/fx/PhaserProcessor.hpp"
#include "../dsp/fx/JunoChorus.hpp"
#include "../dsp/fx/DistortionProcessor.hpp"

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
        "AD_ENVELOPE",
        "Attack-Decay envelope for percussive sounds — ignores gate_off, completes Decay regardless",
        [](int sr) { return std::make_unique<ADEnvelopeProcessor>(sr); }
    );
    reg.register_module(
        "VCA",
        "Voltage-controlled amplifier — scales audio by a gain CV signal",
        [](int sr) { return std::make_unique<VcaProcessor>(sr); }
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
        [](int sr) { return std::make_unique<InverterProcessor>(sr); }
    );
    reg.register_module(
        "CV_MIXER",
        "4-input CV mixer/attenuverter with DC offset — sums bipolar control signals",
        [](int sr) { return std::make_unique<CvMixerProcessor>(sr); }
    );
    reg.register_module(
        "CV_SPLITTER",
        "1-to-4 CV fan-out with per-output gain scaling",
        [](int sr) { return std::make_unique<CvSplitterProcessor>(sr); }
    );
    reg.register_module(
        "MATHS",
        "Slew limiter / portamento: rise/fall time control with linear or exponential curve",
        [](int sr) { return std::make_unique<MathsProcessor>(sr); }
    );
    reg.register_module(
        "GATE_DELAY",
        "Gate delay / pulse shaper: delays note-on gate by a fixed time before firing",
        [](int sr) { return std::make_unique<GateDelayProcessor>(sr); }
    );
    reg.register_module(
        "SAMPLE_HOLD",
        "Sample & hold: freezes cv_in on each rising clock_in edge for stepped modulation",
        [](int /*sr*/) { return std::make_unique<SampleHoldProcessor>(); }
    );
    reg.register_module(
        "HIGH_PASS_FILTER",
        "2-pole biquad high-pass filter — brightens by removing low frequencies",
        [](int sr) { return std::make_unique<HighPassFilterProcessor>(sr); }
    );
    reg.register_module(
        "BAND_PASS_FILTER",
        "2-pole biquad band-pass filter — passes a band around center_freq",
        [](int sr) { return std::make_unique<BandPassFilterProcessor>(sr); }
    );
    reg.register_module(
        "RING_MOD",
        "4-quadrant ring modulator: output = audio_in_a × audio_in_b — bell/metallic timbres",
        [](int sr) { return std::make_unique<RingModProcessor>(sr); }
    );
    reg.register_module(
        "AUDIO_SPLITTER",
        "1-to-4 audio fan-out with per-output gain — routes one source to multiple destinations",
        [](int sr) { return std::make_unique<AudioSplitterProcessor>(sr); }
    );
    reg.register_module(
        "AUDIO_MIXER",
        "4-input audio summing mixer: sums up to 4 PORT_AUDIO sources with per-input gain",
        [](int sr) { return std::make_unique<AudioMixerProcessor>(sr); }
    );
    reg.register_module(
        "NOISE_GATE",
        "Threshold-based gate: opens on signal above threshold, closes on silence below it",
        [](int sr) { return std::make_unique<NoiseGateProcessor>(sr); }
    );
    reg.register_module(
        "ENVELOPE_FOLLOWER",
        "Extracts a dynamic control signal (RMS envelope) from the audio input",
        [](int sr) { return std::make_unique<EnvelopeFollowerProcessor>(sr); }
    );
    reg.register_module(
        "REVERB_FREEVERB",
        "Schroeder/Freeverb stereo reverb: 8 parallel combs + 4 series all-pass per channel",
        [](int sr) { return std::make_unique<FreeverbProcessor>(sr); }
    );
    reg.register_module(
        "REVERB_FDN",
        "Jean-Marc Jot FDN reverb: 8-line Householder network with exact T60 feedback gains",
        [](int sr) { return std::make_unique<FdnReverbProcessor>(sr); }
    );
    reg.register_module(
        "PHASER",
        "4/8-stage all-pass phaser with stereo quadrature LFO and feedback comb",
        [](int sr) { return std::make_unique<PhaserProcessor>(sr); }
    );
    reg.register_module(
        "JUNO_CHORUS",
        "Roland Juno-60 BBD stereo chorus emulation — dual-rate modulated delay with stereo width",
        [](int sr) { return std::make_unique<JunoChorus>(sr); }
    );
    reg.register_module(
        "DISTORTION",
        "Guitar-style distortion with pre/post emphasis and 4x oversampling — drive + character blend",
        [](int sr) { return std::make_unique<DistortionProcessor>(sr); }
    );
}

} // namespace audio
