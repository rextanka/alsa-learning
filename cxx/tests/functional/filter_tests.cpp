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
#include "TestHelper.hpp"
#include "../src/core/Voice.hpp"
#include "../src/dsp/oscillator/WavetableOscillatorProcessor.hpp"
#include "../src/dsp/filter/MoogLadderProcessor.hpp"
#include "../src/dsp/filter/DiodeLadderProcessor.hpp"

void test_filter(hal::AudioDriver* driver, const std::string& name, std::unique_ptr<audio::FilterProcessor> filter) {
    if (!test::g_keep_running) return;

    std::cout << "Testing Filter: " << name << std::endl;
    
    int sample_rate = driver->sample_rate();
    auto voice = std::make_unique<audio::Voice>(sample_rate);
    
    // Use Sawtooth for rich harmonics if it's a wavetable oscillator
    if (auto* wavetable = dynamic_cast<audio::WavetableOscillatorProcessor*>(&voice->oscillator())) {
        wavetable->setWaveType(audio::WaveType::Saw);
    }
    
    // Configure filter base parameters
    voice->set_parameter(1, 5000.0f); // Cutoff
    voice->set_parameter(2, 0.7f);    // Resonance
    voice->set_filter_type(std::move(filter));
    
    // Disable default chiff for clean sweep
    voice->matrix().clear_all();

    driver->set_callback([&voice](std::span<float> output) {
        voice->pull(output);
    });
    
    if (!driver->start()) return;
    
    std::cout << "  Playing with sweeping cutoff..." << std::endl;
    voice->note_on(110.0); // A2
    
    // Sweep cutoff down via ModulationMatrix (Inversion check)
    // We'll use the LFO as a fixed source (intensity sweep) or just set_parameter
    for (int i = 0; i < 100 && test::g_keep_running; ++i) {
        float cutoff = 5000.0f * (1.0f - i / 100.0f) + 100.0f;
        voice->set_parameter(1, cutoff);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // Sweep resonance up
    if (test::g_keep_running) {
        std::cout << "  Increasing resonance..." << std::endl;
        for (int i = 0; i < 50 && test::g_keep_running; ++i) {
            voice->filter()->set_resonance(i / 50.0f);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    
    if (test::g_keep_running) {
        voice->note_off();
        test::wait_while_running(1);
    }

    driver->stop();
    std::cout << "  Done." << std::endl;
}

int main() {
    std::cout << "--- Starting Filter Tests ---" << std::endl;

    test::init_test_environment();
    auto driver = test::create_driver();
    if (!driver) return 1;

    int sr = driver->sample_rate();

    test_filter(driver.get(), "Moog Ladder", std::make_unique<audio::MoogLadderProcessor>(sr));
    
    if (test::g_keep_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        test_filter(driver.get(), "Diode Ladder (TB-303 Style)", std::make_unique<audio::DiodeLadderProcessor>(sr));
    }

    test::cleanup_test_environment(driver.get());
    std::cout << "--- Tests Completed ---" << std::endl;
    return 0;
}
