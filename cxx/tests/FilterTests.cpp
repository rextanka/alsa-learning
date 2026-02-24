/**
 * @file FilterTests.cpp
 * @brief Audible tests for the Moog and Diode ladder filters.
 */

#include <iostream>
#include <memory>
#include <functional>
#include <span>
#include <thread>
#include <chrono>
#include <string>
#include <algorithm>
#include "../src/core/Voice.hpp"
#include "../src/core/AudioSettings.hpp"
#include "../src/dsp/filter/MoogLadderProcessor.hpp"
#include "../src/dsp/filter/DiodeLadderProcessor.hpp"
#include "../src/hal/AudioDriver.hpp"

#ifdef __APPLE__
#include "../src/hal/coreaudio/CoreAudioDriver.hpp"
using NativeDriver = hal::CoreAudioDriver;
#elif defined(__linux__)
#include "../src/hal/alsa/AlsaDriver.hpp"
using NativeDriver = hal::AlsaDriver;
#else
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

void test_filter(const std::string& name, std::unique_ptr<audio::FilterProcessor> filter) {
    std::cout << "Testing Filter: " << name << std::endl;
    
    auto& settings = audio::AudioSettings::instance();
    int sample_rate = settings.sample_rate.load();
    int block_size = settings.block_size.load();

    auto driver = std::make_unique<NativeDriver>(sample_rate, block_size);
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

    // Initialize audio settings for the test environment (e.g., 48kHz/512 blocks for Razer laptop)
#ifdef __linux__
    auto& settings = audio::AudioSettings::instance();
    settings.sample_rate = 48000;
    settings.block_size = 512;
#endif

    int sr = audio::AudioSettings::instance().sample_rate.load();

#if defined(__APPLE__) || defined(__linux__)
    test_filter("Moog Ladder", std::make_unique<audio::MoogLadderProcessor>(sr));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    test_filter("Diode Ladder (TB-303 Style)", std::make_unique<audio::DiodeLadderProcessor>(sr));
#else
    std::cout << "Tests currently only audible on macOS (CoreAudio) or Linux (ALSA)." << std::endl;
#endif

    std::cout << "--- Tests Completed ---" << std::endl;
    return 0;
}
