#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include "CInterface.h"

int main() {
    std::cout << "--- SH-101 Live Hardware Verification ---" << std::endl;
    
    // 1. Initialize Engine
    EngineHandle engine = engine_create(48000);
    if (!engine) {
        std::cerr << "Failed to create engine" << std::endl;
        return 1;
    }

    // 2. Load Patch
    if (engine_load_patch(engine, "../assets/patches/sh101_bass.json") != 0) {
        std::cout << "Patch not found, using manual setup..." << std::endl;
        // Manual setup fallback
        engine_set_modulation(engine, MOD_SRC_ENVELOPE, MOD_TGT_CUTOFF, 1.5f);
        set_param(engine, "pulse_gain", 0.8f);
        set_param(engine, "sub_gain", 0.6f);
        set_param(engine, "vcf_cutoff", 400.0f);
        set_param(engine, "attack", 0.005f);
        set_param(engine, "decay", 0.15f);
        set_param(engine, "sustain", 0.1f);
        set_param(engine, "release", 0.1f);
    }

    // 3. Safety Gain (Hardcoded limit)
    // In our architecture, base_amplitude is used as master gain per voice.
    // For a live test, we'll ensure the driver output is scaled.
    // Since we don't have a global master gain in CInterface yet, we use set_param.
    
    // 4. Start Driver
    std::cout << "Starting Audio Driver..." << std::endl;
    if (engine_start(engine) != 0) {
        std::cerr << "Failed to start audio driver" << std::endl;
        return 1;
    }

    // 5. Play Sequence: C1 to C0 chromatic plucks
    std::cout << "Playing 101 Pluck Sequence..." << std::endl;
    int notes[] = {36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24};
    
    for (int note : notes) {
        std::cout << "Note: " << note << std::endl;
        engine_note_on(engine, note, 0.8f);
        std::this_thread::sleep_for(std::chrono::milliseconds(250)); // 16th note at 60bpmish
        engine_note_off(engine, note);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "Sequence Complete." << std::endl;
    std::cout << "Press ENTER to end test..." << std::endl;
    std::cin.get();

    engine_stop(engine);
    engine_destroy(engine);

    return 0;
}
