/**
 * @file StereoPolyTest.cpp
 * @brief Test polyphony and stereo panning.
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
    std::cout << "--- Starting Stereo Polyphonic Test ---" << std::endl;

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

    std::cout << "Playing E#m chord with panning..." << std::endl;
    
    // We need to access voices to set pan. For this test, we'll use a hack or assume
    // the manager can set it. Since we didn't add engine_set_voice_pan to the bridge,
    // let's do a quick internal access for this test.
    // In a real app, the manager would handle this via MIDI/CC.
    
    // Since we are in C++ test, we can use a friend or just expose voices for testing.
    // For now, let's just trigger the notes. To verify panning, I'll add a quick
    // note_on_panned to the VoiceManager for this implementation.

    // Root: E#4 (65) -> Hard Left (-1.0)
    engine->note_on_panned(65, 0.8f, -1.0f);
    // Third: A4 (69) -> Center (0.0)
    engine->note_on_panned(69, 0.8f, 0.0f);
    // Fifth: C5 (72) -> Hard Right (1.0)
    engine->note_on_panned(72, 0.8f, 1.0f);

    std::this_thread::sleep_for(std::chrono::seconds(3));

    std::cout << "Releasing notes..." << std::endl;
    engine->note_off(65);
    engine->note_off(69);
    engine->note_off(72);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    driver->stop();
    std::cout << "--- Test Completed ---" << std::endl;

    return 0;
}
