/**
 * @file EngineTests.cpp
 * @brief C++ implementation of the oscillator CLI tests using C API.
 */

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include <span>
#include <functional>
#include "../src/hal/AudioDriver.hpp"
#include "../include/CInterface.h"

#ifdef __APPLE__
#include "../hal/coreaudio/CoreAudioDriver.hpp"
using NativeDriver = hal::CoreAudioDriver;
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

void run_test(const std::string& name, int wave, double freq, double total_duration) {
    std::cout << "Testing: " << name << std::endl;
    
    unsigned int sample_rate = 44100;
    auto driver = std::make_unique<NativeDriver>(sample_rate, 512);
    OscillatorHandle osc = oscillator_create(wave, sample_rate);
    
    // Quick ADSR for standalone osc
    EnvelopeHandle env = envelope_create(ENV_ADSR, sample_rate);
    set_param(env, "attack", 0.05f);
    set_param(env, "release", 0.05f);
    
    oscillator_set_frequency(osc, freq);
    
    std::vector<float> osc_buffer(512);
    std::vector<float> env_buffer(512);

    driver->set_callback([osc, env, &osc_buffer, &env_buffer](std::span<float> output) {
        oscillator_process(osc, osc_buffer.data(), output.size());
        envelope_process(env, env_buffer.data(), output.size());
        for (size_t i = 0; i < output.size(); ++i) {
            output[i] = osc_buffer[i] * env_buffer[i];
        }
    });
    
    if (!driver->start()) return;
    
    envelope_gate_on(env);
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long>(total_duration * 1000)));
    envelope_gate_off(env);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    driver->stop();
    oscillator_destroy(osc);
    envelope_destroy(env);
    std::cout << "  Done." << std::endl;
}

int main() {
    std::cout << "--- Starting C++ Oscillator Tests (C API) ---" << std::endl;

#ifdef __APPLE__
    run_test("Static Sine Wave (440Hz)", OSC_WAVETABLE_SINE, 440.0, 1.0);
    run_test("Static Square Wave (220Hz)", OSC_WAVETABLE_SQUARE, 220.0, 1.0);
    run_test("Static Saw Wave (110Hz)", OSC_WAVETABLE_SAW, 110.0, 1.0);
#else
    std::cout << "Tests currently only audible on macOS." << std::endl;
#endif

    std::cout << "--- Tests Completed ---" << std::endl;
    return 0;
}
