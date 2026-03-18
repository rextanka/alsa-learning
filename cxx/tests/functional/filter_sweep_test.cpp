/**
 * @file filter_sweep_test.cpp
 * @brief Functional test of all four filter types using a sawtooth drone
 *        and a logarithmic cutoff sweep.
 *
 * Chain (per sweep): COMPOSITE_GENERATOR → <filter> → VCA ← ADSR_ENVELOPE
 *
 * Each sweep builds an explicit chain with the filter as a first-class node,
 * wires audio connections (VCO→VCF, VCF→VCA) and CV connection (ENV→VCA),
 * then performs a 200-step logarithmic sweep from 8 kHz to 100 Hz.
 *
 * Four sweeps:
 *   1. Moog Ladder   (MOOG_FILTER)  — 4-pole, 24 dB/oct, smooth/creamy
 *   2. TB-303 Diode  (DIODE_FILTER) — 3/4-pole blend, 18-24 dB/oct, rubbery acid
 *   3. SH-101 CEM    (SH_FILTER)    — 4-pole, 24 dB/oct, clean/liquid
 *   4. MS-style      (MS20_FILTER)  — 2-pole HP+LP, 12 dB/oct, aggressive/gritty
 *
 * Expected: each sweep audibly distinct in character; no digital clipping,
 * no silence, no NaN/inf artefacts during the sweep.
 */

#include "../TestHelper.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>

int main() {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "All-filter sweep: Moog-style, TB-style Diode, SH-style CEM, MS-style SVF.",
        "Each filter inserted as first-class chain node; cutoff swept 8 kHz→100 Hz.",
        "VCO → VCF (<filter type>) → VCA ← ADSR_ENVELOPE",
        "Four audibly distinct resonant sweeps — smooth, no stepping, no silence.",
        sample_rate
    );

    // Run one sweep per filter type.
    // Each sweep owns its own EngineWrapper so chains are fully independent.
    struct SweepConfig {
        const char* label;
        const char* module_type;
        float       resonance;
    };

    static const SweepConfig sweeps[] = {
        { "Moog Ladder  (MOOG_FILTER) — 24 dB/oct, smooth/creamy",  "MOOG_FILTER",  0.85f },
        { "TB-style Diode (DIODE_FILTER) — 18-24 dB/oct, rubbery acid", "DIODE_FILTER", 0.70f },
        { "SH-style CEM   (SH_FILTER)   — 24 dB/oct, clean/liquid",    "SH_FILTER",    0.82f },
        { "MS-style       (MS20_FILTER) — 12 dB/oct, aggressive/gritty", "MS20_FILTER", 0.78f },
    };

    for (const auto& cfg : sweeps) {
        std::cout << "\n================================================================\n"
                  << "SWEEP: " << cfg.label << "\n"
                  << "================================================================\n";

        test::EngineWrapper engine(sample_rate);

        // Build chain: VCO → VCF → VCA; ENV in mod_sources_
        engine_add_module(engine.get(), "COMPOSITE_GENERATOR", "VCO");
        engine_add_module(engine.get(), cfg.module_type,       "VCF");
        engine_add_module(engine.get(), "ADSR_ENVELOPE",       "ENV");
        engine_add_module(engine.get(), "VCA",                 "VCA");

        // Audio path connections (required by bake() for port-type validation)
        engine_connect_ports(engine.get(), "VCO", "audio_out",    "VCF", "audio_in");
        engine_connect_ports(engine.get(), "VCF", "audio_out",    "VCA", "audio_in");
        // CV connection: envelope gates the VCA
        engine_connect_ports(engine.get(), "ENV", "envelope_out", "VCA", "gain_cv");

        engine_bake(engine.get());

        // Sawtooth drone — zero all other gains first
        set_param(engine.get(), "pulse_gain",    0.0f);
        set_param(engine.get(), "sub_gain",      0.0f);
        set_param(engine.get(), "sine_gain",     0.0f);
        set_param(engine.get(), "triangle_gain", 0.0f);
        set_param(engine.get(), "saw_gain",      1.0f);
        set_param(engine.get(), "amp_sustain",   1.0f);

        // Starting filter state
        set_param(engine.get(), "vcf_cutoff", 8000.0f);
        set_param(engine.get(), "vcf_res",    cfg.resonance);

        if (engine_start(engine.get()) != 0) {
            std::cerr << "[FAIL] engine_start failed for " << cfg.label << "\n";
            return 1;
        }

        // Trigger A2 (~110 Hz) for tonal reference
        engine_note_on(engine.get(), 45, 1.0f);

        // Logarithmic sweep: 200 steps over 2 seconds (10 ms per step)
        constexpr int   STEPS       = 200;
        constexpr float START_FREQ  = 8000.0f;
        constexpr float END_FREQ    = 100.0f;
        constexpr int   STEP_MS     = 10;

        std::cout << "[SWEEP] " << STEPS << " steps, "
                  << START_FREQ << " Hz → " << END_FREQ << " Hz\n";

        for (int i = 0; i <= STEPS; ++i) {
            float t      = static_cast<float>(i) / STEPS;
            float cutoff = START_FREQ * std::pow(END_FREQ / START_FREQ, t);
            set_param(engine.get(), "vcf_cutoff", cutoff);
            std::this_thread::sleep_for(std::chrono::milliseconds(STEP_MS));
        }

        engine_note_off(engine.get(), 45);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        engine_stop(engine.get());

        std::cout << "[DONE] Sweep completed: " << cfg.label << "\n";
    }

    std::cout << "\n[SUCCESS] All four filter sweeps completed.\n";
    return 0;
}
