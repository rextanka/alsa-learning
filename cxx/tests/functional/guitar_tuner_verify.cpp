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

const std::map<std::string, double> guitar_tuning = {
    {"E2", 82.41},  // 6th String
    {"A2", 110.00}, // 5th String
    {"D3", 146.83}, // 4th String
    {"G3", 196.00}, // 3rd String
    {"B3", 246.94}, // 2nd String
    {"E4", 329.63}  // 1st String
};

void print_help() {
    std::cout << "Guitar Tuner Verification Utility\n";
    std::cout << "Usage: guitar_tuner_verify [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help            Show this help message\n";
    std::cout << "  -n, --note <note>     Play a specific string (E2, A2, D3, G3, B3, E4)\n";
    std::cout << "  -d, --duration <sec>  Set play duration in seconds (default: 5.0)\n";
    std::cout << "  -o, --osc <type>      Select oscillator (sine, saw, square, triangle) (default: sine)\n";
    std::cout << "  -c, --cycle           Cycle through all strings (default if no note specified)\n\n";
    std::cout << "Guitar Reference Table:\n";
    for (const auto& [name, freq] : guitar_tuning) {
        std::cout << "  " << std::left << std::setw(3) << name << ": " << std::fixed << std::setprecision(2) << freq << " Hz\n";
    }
}

int main(int argc, char* argv[]) {
    test::init_test_environment();
    std::string target_note = "";
    double duration = 5.0;
    std::string osc_type_str = "sine";
    bool cycle = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help();
            return 0;
        } else if ((arg == "-n" || arg == "--note") && i + 1 < argc) {
            target_note = argv[++i];
        } else if ((arg == "-d" || arg == "--duration") && i + 1 < argc) {
            duration = std::stod(argv[++i]);
        } else if ((arg == "-o" || arg == "--osc") && i + 1 < argc) {
            osc_type_str = argv[++i];
        } else if (arg == "-c" || arg == "--cycle") {
            cycle = true;
        }
    }

    if (target_note.empty() && !cycle) {
        cycle = true;
    }

    // Map oscillator type
    int osc_type = WAVE_SINE;
    if (osc_type_str == "saw") osc_type = WAVE_SAW;
    else if (osc_type_str == "square") osc_type = WAVE_SQUARE;
    else if (osc_type_str == "triangle") osc_type = WAVE_TRIANGLE;

    int sample_rate = test::get_safe_sample_rate(0);
    PRINT_TEST_HEADER("Guitar Tuner Verification", 
                      "Qualitative pitch audit of oscillator frequency accuracy.", 
                      "VCO -> Output", 
                      "External tuner agreement with played frequency.", 
                      sample_rate);

    test::EngineWrapper engine(sample_rate);
    
    // --- Tier 1 (Direct Path) Initialization ---
    // Protocol Step 1 & 2 handled by EngineWrapper + ctor.
    // Protocol Step 3: Modular Patching (Clear ADSR -> VCA link for Tier 1 raw audit)
    engine_clear_modulations(engine.get());

    // Protocol Step 4: ADSR Arming (Manually open VCA gate for steady tone)
    set_param(engine.get(), "amp_base", 1.0f); 
    set_param(engine.get(), "amp_attack", 0.001f);
    set_param(engine.get(), "amp_sustain", 1.0f);
    
    // Protocol Step 5: Lifecycle Start
    engine_start(engine.get());

    auto play_note = [&](std::string note, double freq) {
        std::cout << "\n[MANUAL CHECK] Playing " << note << " (" << freq << " Hz) using " << osc_type_str << " oscillator...\n";
        
        // Reset all gains first
        set_param(engine.get(), "sine_gain", 0.0f);
        set_param(engine.get(), "saw_gain", 0.0f);
        set_param(engine.get(), "pulse_gain", 0.0f);
        set_param(engine.get(), "sub_gain", 0.0f);
        set_param(engine.get(), "triangle_gain", 0.0f);

        // Map oscillator type to Gain parameter
        std::string gain_param = "sine_gain"; // default
        if (osc_type_str == "saw") gain_param = "saw_gain";
        else if (osc_type_str == "square") gain_param = "pulse_gain";
        else if (osc_type_str == "triangle") gain_param = "triangle_gain";
        
        set_param(engine.get(), gain_param.c_str(), 0.8f); 
        
        // Ensure VCA is wide open
        set_param(engine.get(), "amp_attack", 0.001f);
        set_param(engine.get(), "amp_sustain", 1.0f);
        set_param(engine.get(), "vcf_cutoff", 20000.0f);

        // We use engine_note_on which calculates frequency from MIDI note, 
        // but here we want exact frequency.
        // The Engine currently doesn't expose an engine_note_on_freq in CInterface,
        // but it does have internal frequency calculation.
        // We'll use a hacky way by finding the closest MIDI note or 
        // ideally we would have added engine_note_on_freq.
        // Wait, CInterface has oscillator_set_frequency for raw oscillators, 
        // but Engine handles polyphony.
        
        // For the purpose of this tuner tool, we'll use note_on and then 
        // immediately override the frequency of the active voice if possible,
        // or just use the closest MIDI note and accept that the engine's 
        // pitch accuracy at MIDI notes is what we are testing.
        
        // Frequency to MIDI note: n = 69 + 12 * log2(f / 440)
        double midi_note = 69.0 + 12.0 * log2(freq / 440.0);
        int note_num = static_cast<int>(round(midi_note));
        
        engine_note_on(engine.get(), note_num, 1.0f);

        auto start = std::chrono::steady_clock::now();
        while (true) {
            auto now = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed = now - start;
            if (elapsed.count() >= duration) break;

            double remaining = duration - elapsed.count();
            std::cout << "\r[" << std::fixed << std::setprecision(1) << remaining << "s] " << std::flush;
            
            // Progress bar
            int bar_width = 20;
            int pos = static_cast<int>(bar_width * (elapsed.count() / duration));
            std::cout << "[";
            for (int i = 0; i < bar_width; ++i) {
                if (i < pos) std::cout << "=";
                else if (i == pos) std::cout << ">";
                else std::cout << " ";
            }
            std::cout << "] " << std::flush;

            std::this_thread::sleep_for(100ms);
        }
        std::cout << "\r[ DONE ] " << std::string(40, ' ') << "\n";
        
        engine_note_off(engine.get(), note_num);
        std::this_thread::sleep_for(200ms); // Gap between notes
    };

    if (cycle) {
        // Sort order for cycling: E2, A2, D3, G3, B3, E4
        std::vector<std::string> order = {"E2", "A2", "D3", "G3", "B3", "E4"};
        for (const auto& note : order) {
            play_note(note, guitar_tuning.at(note));
        }
    } else {
        if (guitar_tuning.count(target_note)) {
            play_note(target_note, guitar_tuning.at(target_note));
        } else {
            std::cerr << "Unknown note: " << target_note << "\n";
            print_help();
            return 1;
        }
    }

    std::cout << "\nVerification Session Complete.\n";
    return 0;
}
