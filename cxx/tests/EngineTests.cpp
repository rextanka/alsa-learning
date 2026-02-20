/**
 * @file EngineTests.cpp
 * @brief C++ implementation of the oscillator CLI tests.
 */

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <string>
#include "../audio/Voice.hpp"
#include "../hal/include/AudioDriver.hpp"

#ifdef __APPLE__
#include "../hal/coreaudio/CoreAudioDriver.hpp"
using NativeDriver = hal::CoreAudioDriver;
#else
// Placeholder for Linux/ALSA
namespace hal { class DummyDriver : public AudioDriver { 
public: 
    bool start() override { return true; } 
    void stop() override {} 
    void set_callback(AudioCallback) override {} 
    int sample_rate() const override { return 44100; }
    int block_size() const override { return 512; }
}; }
using NativeDriver = hal::DummyDriver;
#endif

void run_test(const std::string& name, audio::WaveType wave, double freq, double target_freq, double glide_duration, double total_duration) {
    std::cout << "Testing: " << name << std::endl;
    
    int sample_rate = 44100;
    auto driver = std::make_unique<NativeDriver>(sample_rate, 512);
    auto voice = std::make_unique<audio::Voice>(sample_rate);
    
    voice->oscillator().setWaveType(wave);
    
    // Quick envelope for audible tests
    voice->envelope().set_attack_time(0.05f);
    voice->envelope().set_release_time(0.05f);
    
    driver->set_callback([&voice](std::span<float> output) {
        voice->pull(output);
    });
    
    if (!driver->start()) {
        std::cerr << "  Failed to start audio driver" << std::endl;
        return;
    }
    
    voice->note_on(freq);
    
    if (glide_duration > 0) {
        voice->oscillator().setFrequencyGlide(target_freq, glide_duration);
    } else if (target_freq != freq && target_freq > 0) {
        // Simple sweep if no glide specified but target differs
        // For a true sweep we'd need a continuous update or a specific processor,
        // but setFrequencyGlide handles it.
        voice->oscillator().setFrequencyGlide(target_freq, total_duration);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long>(total_duration * 1000)));
    
    voice->note_off();
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Wait for release
    
    driver->stop();
    std::cout << "  Done." << std::endl;
}

int main() {
    std::cout << "--- Starting C++ Oscillator Tests ---" << std::endl;

#ifdef __APPLE__
    // 1. Static Sine Wave (440Hz)
    run_test("Static Sine Wave (440Hz)", audio::WaveType::Sine, 440.0, 440.0, 0, 1.0);

    // 2. Static Square Wave (220Hz)
    run_test("Static Square Wave (220Hz)", audio::WaveType::Square, 220.0, 220.0, 0, 1.0);

    // 3. Sawtooth Sweep (100Hz to 1000Hz)
    run_test("Sawtooth Sweep (100Hz to 1000Hz)", audio::WaveType::Saw, 100.0, 1000.0, 0, 2.0);

    // 4. Triangle Glide (880Hz down to 440Hz)
    run_test("Triangle Glide (880Hz down to 440Hz)", audio::WaveType::Triangle, 880.0, 440.0, 0.5, 2.0);
#else
    std::cout << "Tests currently only audible on macOS via CoreAudio." << std::endl;
#endif

    std::cout << "--- Tests Completed ---" << std::endl;
    return 0;
}
