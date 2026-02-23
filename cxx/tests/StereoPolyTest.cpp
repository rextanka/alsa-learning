/**
 * @file StereoPolyTest.cpp
 * @brief Test polyphony and gradual stereo panning using C API.
 */

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <string>
#include <functional>
#include "../src/core/AudioBuffer.hpp"
#include "../include/CInterface.h"

// Note: For real-time audio in this test, we'll still use the HAL C++ classes
// but we will wrap the engine calls exclusively in the C API.
#ifdef __APPLE__
#include "../hal/coreaudio/CoreAudioDriver.hpp"
using NativeDriver = hal::CoreAudioDriver;
#else
#include "../src/hal/AudioDriver.hpp"
namespace hal { class DummyDriver : public AudioDriver { 
public: 
    DummyDriver(int /*sr*/, int /*bs*/) {}
    bool start() override { return true; } 
    void stop() override {} 
    void set_callback(AudioCallback) override {} 
    void set_stereo_callback(StereoAudioCallback) override {}
    int sample_rate() const override { return 44100; }
    int block_size() const override { return 512; }
}; }
using NativeDriver = hal::DummyDriver;
#endif

int main() {
    std::cout << "--- Starting Stereo Polyphonic Test (C API) ---" << std::endl;

    unsigned int sample_rate = 44100;
    auto driver = std::make_unique<NativeDriver>(sample_rate, 512);
    
    // Use C API to create engine
    EngineHandle engine = engine_create(sample_rate);

    driver->set_stereo_callback([engine](audio::AudioBuffer& output) {
        // Use C API for processing
        engine_process(engine, output.left.data(), output.left.size());
        // Since engine_process currently assumes mono output, we copy to right
        // In a real stereo engine process, it would fill both.
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

    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "Step 2: Gradual Panning (over 1s)..." << std::endl;
    const int steps = 50;
    for (int i = 1; i <= steps; ++i) {
        float t = static_cast<float>(i) / steps;
        engine_set_note_pan(engine, 65, -t);
        engine_set_note_pan(engine, 72, t);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    std::cout << "Step 3: Holding panned chord..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "Step 4: Releasing notes..." << std::endl;
    engine_note_off(engine, 65);
    engine_note_off(engine, 69);
    engine_note_off(engine, 72);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    driver->stop();
    engine_destroy(engine);
    std::cout << "--- Test Completed ---" << std::endl;

    return 0;
}
