/**
 * @file oscillator_baseline_test.cpp
 * @brief Direct hardware-to-oscillator validation with ADSR envelope gating using Bridge API.
 */

#include "../TestHelper.hpp"
#include <iostream>
#include <chrono>
#include <thread>

int main() {
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Oscillator & ADSR Baseline",
        "Direct validation of Sine VCO + ADSR envelope via Bridge API.",
        "Sine VCO -> VCA (ADSR) -> Bridge -> Output",
        "Cycling A4 (1s ON, 1s OFF) with 50ms attack and 200ms release for 10s.",
        sample_rate
    );

    test::init_test_environment();
    test::EngineWrapper engine(sample_rate);

    // Configure ADSR
    set_param(engine.get(), "amp_attack", 0.05f);
    set_param(engine.get(), "amp_decay", 0.1f);
    set_param(engine.get(), "amp_sustain", 0.7f);
    set_param(engine.get(), "amp_release", 0.2f);

    if (engine_start(engine.get()) != 0) {
        std::cerr << "Failed to start engine" << std::endl;
        return 1;
    }

    std::cout << "Starting 10-second cycling validation..." << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    bool last_gate = false;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed_total = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
        if (elapsed_total >= 10) break;

        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        bool current_gate = (elapsed_ms % 2000) < 1000;

        if (current_gate != last_gate) {
            if (current_gate) {
                std::cout << "[GATE ON] Triggering A4" << std::endl;
                engine_note_on_name(engine.get(), "A4", 0.8f);
            } else {
                std::cout << "[GATE OFF] Releasing A4" << std::endl;
                engine_note_off_name(engine.get(), "A4");
            }
            last_gate = current_gate;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    engine_stop(engine.get());
    std::cout << "--- Baseline Verification Done. Engine destroyed via RAII. ---" << std::endl;
    return 0;
}
