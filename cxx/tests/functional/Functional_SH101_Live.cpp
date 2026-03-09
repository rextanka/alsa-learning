#include "../TestHelper.hpp"
#include <iostream>
#include <vector>
#include <thread> 
#include <chrono>

/**
 * @file Functional_SH101_Live.cpp
 * @brief Live functional test for SH-101 style bass output via Bridge API.
 */
int main() {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "SH-101 Live Pluck Sequence",
        "Verifies SH-101 style bass patch loading and chromatic performance.",
        "Patch (JSON) -> Engine -> VCF -> VCA -> Output",
        "Audible chromatic descending pluck sequence (C1 to C0).",
        sample_rate
    );

    test::EngineWrapper engine(sample_rate);

    // Rely solely on patch loading
    const char* patch_path = "cxx/assets/patches/sh101_bass.json";
    std::cout << "📂 Loading patch: " << patch_path << std::endl;
    
    if (engine_load_patch(engine.get(), patch_path) != 0) {
        // Try absolute path or search up
        patch_path = "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/assets/patches/sh101_bass.json";
        std::cout << "📂 Trying absolute path: " << patch_path << std::endl;
        if (engine_load_patch(engine.get(), patch_path) != 0) {
             std::cerr << "❌ Failed to load patch 'sh101_bass.json'." << std::endl;
             return 1;
        }
    }

    if (engine_start(engine.get()) != 0) {
        std::cerr << "❌ Failed to start audio driver" << std::endl;
        return 1;
    }

    std::cout << "🔊 Playing chromatic pluck sequence (C1 to C0)..." << std::endl;
    
    int midi_notes[] = {36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24};
    
    for (int note : midi_notes) {
        std::cout << "🎹 Note ON: " << note << std::endl;
        engine_note_on(engine.get(), note, 0.8f);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        std::cout << "🎹 Note OFF: " << note << std::endl;
        engine_note_off(engine.get(), note);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    engine_stop(engine.get());
    std::cout << "✅ Done. Engine destroyed via RAII." << std::endl;
    return 0;
}
