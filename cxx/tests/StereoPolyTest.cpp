/**
 * @file StereoPolyTest.cpp
 * @brief Test polyphony and gradual stereo panning using C API.
 */

#include <iostream>
#include <memory>
#include <vector>
#include <algorithm>
#include "TestHelper.hpp"
#include "../include/CInterface.h"

int main() {
    std::cout << "--- Starting Stereo Polyphonic Test (C API) ---" << std::endl;

    test::init_test_environment();
    auto driver = test::create_driver();
    if (!driver) return 1;

    unsigned int sample_rate = static_cast<unsigned int>(driver->sample_rate());
    EngineHandle engine = engine_create(sample_rate);

    driver->set_stereo_callback([engine](audio::AudioBuffer& output) {
        // Use C API for processing
        // Task 2: The engine internal VoiceManager already sums voices.
        // We just need to make sure the C API pulls into a buffer.
        engine_process(engine, output.left.data(), output.left.size());
        
        // Temporarily copy L to R as our engine currently outputs mono via engine_process.
        // In the future, engine_process will support stereo AudioBuffer.
        std::copy(output.left.begin(), output.left.end(), output.right.begin());
    });

    if (!driver->start()) {
        std::cerr << "Failed to start audio driver" << std::endl;
        return 1;
    }

    std::cout << "Step 1: Playing E#m chord (Centered)..." << std::endl;
    engine_note_on(engine, 65, 0.8f);
    engine_note_on(engine, 69, 0.8f);
    engine_note_on(engine, 72, 0.8f);

    test::wait_while_running(1);

    if (test::g_keep_running) {
        std::cout << "Step 2: Gradual Panning (over 1s)..." << std::endl;
        const int steps = 50;
        for (int i = 1; i <= steps && test::g_keep_running; ++i) {
            float t = static_cast<float>(i) / steps;
            engine_set_note_pan(engine, 65, -t);
            engine_set_note_pan(engine, 72, t);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    if (test::g_keep_running) {
        std::cout << "Step 3: Holding panned chord..." << std::endl;
        test::wait_while_running(2);
    }

    if (test::g_keep_running) {
        std::cout << "Step 4: Releasing notes..." << std::endl;
        engine_note_off(engine, 65);
        engine_note_off(engine, 69);
        engine_note_off(engine, 72);
        test::wait_while_running(1);
    }

    test::cleanup_test_environment(driver.get());
    engine_destroy(engine);
    std::cout << "--- Test Completed ---" << std::endl;

    return 0;
}
