#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cmath>
#include "CInterface.h"

int main() {
    std::cout << "--- Juno Chorus Live Sweep Test ---" << std::endl;
    
    // 1. Initialize Engine
    EngineHandle engine = engine_create(48000);
    if (!engine) {
        std::cerr << "Failed to create engine" << std::endl;
        return 1;
    }

    // 2. Start Driver
    std::cout << "Starting Audio Driver..." << std::endl;
    if (engine_start(engine) != 0) {
        std::cerr << "Failed to start audio driver" << std::endl;
        return 1;
    }

    // 3. Trigger static Sawtooth for Chorus auditing
    std::cout << "Triggering static Sawtooth..." << std::endl;
    engine_note_on(engine, 48, 0.7f); // C2

    // 4. Linear period sweep from 0.1Hz to 8Hz
    std::cout << "Sweeping Chorus LFO rate..." << std::endl;
    float start_rate = 0.1f;
    float end_rate = 8.0f;
    int steps = 100;
    
    // Switch to Chorus Mode I
    set_param(engine, "chorus_mode", 1); 

    for (int i = 0; i <= steps; ++i) {
        // Linear in period sweep
        float t = static_cast<float>(i) / steps;
        float start_period = 1.0f / start_rate;
        float end_period = 1.0f / end_rate;
        float current_period = start_period + t * (end_period - start_period);
        float current_rate = 1.0f / current_period;
        
        set_param(engine, "chorus_rate", current_rate);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "Sweep Complete." << std::endl;
    
    // 5. Hot-switching demonstration
    std::cout << "Hot-switching modes (I -> II -> I+II)..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    set_param(engine, "chorus_mode", 2);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    set_param(engine, "chorus_mode", 3);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "Press ENTER to end test..." << std::endl;
    std::cin.get();

    engine_stop(engine);
    engine_destroy(engine);

    return 0;
}
