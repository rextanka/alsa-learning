/**
 * @file StereoPolyTest.cpp
 * @brief Test polyphony and gradual stereo panning.
 */

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <string>
#include "../audio/VoiceManager.hpp"
#include "../hal/include/AudioDriver.hpp"

#ifdef __APPLE__
#include "../hal/coreaudio/CoreAudioDriver.hpp"
using NativeDriver = hal::CoreAudioDriver;
#else
namespace hal { class DummyDriver : public AudioDriver { 
public: 
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
    std::cout << "--- Starting Stereo Polyphonic Test (Gradual Panning) ---" << std::endl;

    int sample_rate = 44100;
    auto driver = std::make_unique<NativeDriver>(sample_rate, 512);
    auto engine = std::make_unique<audio::VoiceManager>(sample_rate);

    // E#m chord (F minor equivalent)
    // E# (F4) = 65, G## (A4) = 69, B# (C5) = 72
    
    driver->set_stereo_callback([&engine](audio::AudioBuffer& output) {
        engine->pull(output);
    });

    if (!driver->start()) {
        std::cerr << "Failed to start audio driver" << std::endl;
        return 1;
    }

    std::cout << "Step 1: Playing E#m chord (Centered)..." << std::endl;
    engine->note_on(65, 0.8f);
    engine->note_on(69, 0.8f);
    engine->note_on(72, 0.8f);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "Step 2: Gradual Panning (over 1s)..." << std::endl;
    std::cout << "  Root -> Hard Left, Third -> Center, Fifth -> Hard Right" << std::endl;

    const int steps = 50;
    const std::chrono::milliseconds step_duration(20); // 50 * 20ms = 1000ms

    for (int i = 1; i <= steps; ++i) {
        float t = static_cast<float>(i) / steps;
        
        // Root: 0.0 to -1.0
        engine->set_note_pan(65, -t);
        // Third: stays at 0.0
        engine->set_note_pan(69, 0.0f);
        // Fifth: 0.0 to 1.0
        engine->set_note_pan(72, t);

        std::this_thread::sleep_for(step_duration);
    }

    std::cout << "Step 3: Holding panned chord..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "Step 4: Releasing notes..." << std::endl;
    engine->note_off(65);
    engine->note_off(69);
    engine->note_off(72);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    driver->stop();
    std::cout << "--- Test Completed ---" << std::endl;

    return 0;
}
