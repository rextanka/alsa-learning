/**
 * @file guitar_tuner_verify.cpp
 * @brief Guitar tuner verification utility — qualitative pitch audit.
 *
 * Chain: COMPOSITE_GENERATOR -> ADSR_ENVELOPE -> VCA (Phase 15)
 * Plays each guitar string (E3–E5) for a configurable duration so an
 * external tuner can verify oscillator frequency accuracy.
 *
 * Usage: guitar_tuner_verify [-n note] [-d seconds] [-o sine|saw|square|triangle] [-c]
 */

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <iomanip>
#include "CInterface.h"
#include "TestHelper.hpp"

using namespace std::chrono_literals;

static const std::map<std::string, double> guitar_tuning = {
    {"E3", 164.81},
    {"A3", 220.00},
    {"D4", 293.66},
    {"G4", 392.00},
    {"B4", 493.88},
    {"E5", 659.26}
};

static void print_help() {
    std::cout << "Guitar Tuner Verification Utility\n";
    std::cout << "Usage: guitar_tuner_verify [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help            Show this help\n";
    std::cout << "  -n, --note <note>     Play specific string (E3,A3,D4,G4,B4,E5)\n";
    std::cout << "  -d, --duration <sec>  Play duration in seconds (default: 5)\n";
    std::cout << "  -o, --osc <type>      Oscillator: sine, saw, square, triangle\n";
    std::cout << "  -c, --cycle           Cycle through all strings (default)\n\n";
    std::cout << "Guitar Reference Table:\n";
    for (const auto& [name, freq] : guitar_tuning)
        std::cout << "  " << std::left << std::setw(3) << name
                  << ": " << std::fixed << std::setprecision(2) << freq << " Hz\n";
}

int main(int argc, char* argv[]) {
    test::init_test_environment();

    std::string target_note;
    double duration        = 5.0;
    std::string osc_type   = "sine";
    bool cycle             = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help")        { print_help(); return 0; }
        else if ((arg == "-n" || arg == "--note")     && i + 1 < argc) target_note = argv[++i];
        else if ((arg == "-d" || arg == "--duration") && i + 1 < argc) duration = std::stod(argv[++i]);
        else if ((arg == "-o" || arg == "--osc")      && i + 1 < argc) osc_type = argv[++i];
        else if (arg == "-c" || arg == "--cycle")    cycle = true;
    }

    if (target_note.empty() && !cycle) cycle = true;

    int sample_rate = test::get_safe_sample_rate(0);
    PRINT_TEST_HEADER("Guitar Tuner Verification",
                      "Qualitative pitch audit of oscillator frequency accuracy.",
                      "COMPOSITE_GENERATOR -> ADSR_ENVELOPE -> VCA -> Output",
                      "External tuner agreement with played frequency.",
                      sample_rate);

    test::EngineWrapper engine(sample_rate);

    // Phase 15 chain — open VCA gate by using full sustain
    engine_add_module(engine.get(), "COMPOSITE_GENERATOR", "VCO");
    engine_add_module(engine.get(), "ADSR_ENVELOPE",       "ENV");
    engine_add_module(engine.get(), "VCA",                 "VCA");
    engine_connect_ports(engine.get(), "ENV", "envelope_out", "VCA", "gain_cv");
    engine_bake(engine.get());

    set_param(engine.get(), "amp_attack",  0.001f);
    set_param(engine.get(), "amp_sustain", 1.0f);
    set_param(engine.get(), "vcf_cutoff",  20000.0f);
    engine_start(engine.get());

    // Map oscillator type to gain parameter
    std::string gain_param = "sine_gain";
    if      (osc_type == "saw")      gain_param = "saw_gain";
    else if (osc_type == "square")   gain_param = "pulse_gain";
    else if (osc_type == "triangle") gain_param = "triangle_gain";

    auto play_note = [&](const std::string& note, double freq) {
        std::cout << "\n[MANUAL CHECK] Playing " << note << " (" << freq << " Hz) using "
                  << osc_type << " oscillator...\n";

        // Reset all gains, enable selected waveform
        const char* all[] = {"sine_gain", "saw_gain", "pulse_gain", "triangle_gain", "sub_gain"};
        for (auto g : all) set_param(engine.get(), g, 0.0f);
        set_param(engine.get(), gain_param.c_str(), 0.8f);

        int note_num = static_cast<int>(std::round(69.0 + 12.0 * std::log2(freq / 440.0)));
        engine_note_on(engine.get(), note_num, 1.0f);

        auto start = std::chrono::steady_clock::now();
        while (true) {
            auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (elapsed >= duration) break;
            double remaining = duration - elapsed;
            std::cout << "\r[" << std::fixed << std::setprecision(1) << remaining << "s]" << std::flush;
            std::this_thread::sleep_for(100ms);
        }
        std::cout << "\r[ DONE ] " << std::string(20, ' ') << "\n";

        engine_note_off(engine.get(), note_num);
        std::this_thread::sleep_for(200ms);
    };

    if (cycle) {
        for (const auto& note : std::vector<std::string>{"E3", "A3", "D4", "G4", "B4", "E5"})
            play_note(note, guitar_tuning.at(note));
    } else {
        if (!guitar_tuning.count(target_note)) {
            std::cerr << "Unknown note: " << target_note << "\n";
            print_help();
            return 1;
        }
        play_note(target_note, guitar_tuning.at(target_note));
    }

    std::cout << "\nVerification Session Complete.\n";
    return 0;
}
