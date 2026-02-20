/**
 * @file FilterTests.cpp
 * @brief Audible tests for the Moog and Diode ladder filters.
 */

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <string>
#include "../audio/Voice.hpp"
#include "../audio/filter/MoogLadderProcessor.hpp"
#include "../audio/filter/DiodeLadderProcessor.hpp"
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
    int sample_rate() const override { return 44100; }
    int block_size() const override { return 512; }
}; }
using NativeDriver = hal::DummyDriver;
#endif

void test_filter(const std::string& name, std::unique_ptr<audio::FilterProcessor> filter) {
    std::cout << "Testing Filter: " << name << std::endl;
    
    int sample_rate = 44100;
    auto driver = std::make_unique<NativeDriver>(sample_rate, 512);
    auto voice = std::make_unique<audio::Voice>(sample_rate);
    
    // Use Sawtooth for rich harmonics
    voice->oscillator().setWaveType(audio::WaveType::Saw);
    
    // Configure filter
    filter->set_cutoff(5000.0f);
    filter->set_resonance(0.7f);
    voice->set_filter_type(std::move(filter));
    
    driver->set_callback([&voice](std::span<float> output) {
        voice->pull(output);
    });
    
    if (!driver->start()) return;
    
    std::cout << "  Playing with sweeping cutoff..." << std::endl;
    voice->note_on(110.0); // A2
    
    // Sweep cutoff down
    for (int i = 0; i < 100; ++i) {
        float cutoff = 5000.0f * (1.0f - i / 100.0f) + 100.0f;
        voice->filter()->set_cutoff(cutoff);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // Sweep resonance up
    std::cout << "  Increasing resonance..." << std::endl;
    for (int i = 0; i < 50; ++i) {
        voice->filter()->set_resonance(i / 50.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    voice->note_off();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    driver->stop();
    std::cout << "  Done." << std::endl;
}

int main() {
    std::cout << "--- Starting Filter Tests ---" << std::endl;

#ifdef __APPLE__
    test_filter("Moog Ladder", std::make_unique<audio::MoogLadderProcessor>(44100));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    test_filter("Diode Ladder (TB-303 Style)", std::make_unique<audio::DiodeLadderProcessor>(44100));
#else
    std::cout << "Tests currently only audible on macOS via CoreAudio." << std::endl;
#endif

    std::cout << "--- Tests Completed ---" << std::endl;
    return 0;
}
