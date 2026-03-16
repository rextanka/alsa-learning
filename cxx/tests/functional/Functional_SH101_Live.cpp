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
        "Audible chromatic descending pluck sequence (C2 to C1).",
        sample_rate
    );

    test::EngineWrapper engine(sample_rate);

    const char* patch_path = "patches/sh_bass.json";
    std::cout << "[SH101Live] Loading patch: " << patch_path << std::endl;

    if (engine_load_patch(engine.get(), patch_path) != 0) {
        std::cerr << "[SH101Live] Failed to load patch '" << patch_path << "'." << std::endl;
        return 1;
    }

    if (engine_start(engine.get()) != 0) {
        std::cerr << "[SH101Live] Failed to start audio driver" << std::endl;
        return 1;
    }

    std::cout << "[SH101Live] Playing chromatic pluck sequence (C2 to C1)..." << std::endl;

    int midi_notes[] = {48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36};

    for (int note : midi_notes) {
        std::cout << "[SH101Live] Note ON: " << note << std::endl;
        engine_note_on(engine.get(), note, 0.8f);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        std::cout << "[SH101Live] Note OFF: " << note << std::endl;
        engine_note_off(engine.get(), note);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    engine_stop(engine.get());
    std::cout << "[SH101Live] Done. Engine destroyed via RAII." << std::endl;
    return 0;
}
