#include "CInterface.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cmath>

/**
 * @brief Live functional test for SH-101 style bass output.
 * 
 * This test initializes the engine, loads a patch, and plays a chromatic sequence.
 * Expects 'sh101_bass.json' to be available in the execution directory or assets path.
 */
int main() {
    const unsigned int sample_rate = 44100;
    std::cout << "🚀 Starting Functional_SH101_Live (" << sample_rate << "Hz)" << std::endl;

    EngineHandle engine = engine_create(sample_rate);
    if (!engine) {
        std::cerr << "❌ Failed to create engine" << std::endl;
        return -1;
    }

    // Rely solely on patch loading as per SOP
    const char* patch_path = "cxx/assets/patches/sh101_bass.json";
    std::cout << "📂 Loading patch: " << patch_path << std::endl;
    
    if (engine_load_patch(engine, patch_path) != 0) {
        // Try absolute path or search up
        patch_path = "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/assets/patches/sh101_bass.json";
        std::cout << "📂 Trying absolute path: " << patch_path << std::endl;
        if (engine_load_patch(engine, patch_path) != 0) {
             std::cerr << "❌ Failed to load patch 'sh101_bass.json'. Check paths." << std::endl;
             engine_destroy(engine);
             return -1;
        }
    }

    if (engine_start(engine) != 0) {
        std::cerr << "❌ Failed to start audio driver" << std::endl;
        engine_destroy(engine);
        return -1;
    }

    std::cout << "🔊 Playing chromatic pluck sequence (C1 to C0)..." << std::endl;
    
    // 90/10 Articulation Pluck Sequence
    int midi_notes[] = {36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24};
    
    for (int note : midi_notes) {
        std::cout << "🎹 Note ON: " << note << std::endl;
        engine_note_on(engine, note, 0.8f);
        
        // Hold for 200ms
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        std::cout << "🎹 Note OFF: " << note << std::endl;
        engine_note_off(engine, note);
        
        // Gap between notes
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "🛑 Stopping engine..." << std::endl;
    engine_stop(engine);
    engine_destroy(engine);

    std::cout << "✅ Done." << std::endl;
    return 0;
}
