/**
 * @file AlsaCheck.cpp
 * @brief Utility to verify ALSA driver and audio processing.
 */

#include "../src/hal/alsa/AlsaDriver.hpp"
#include "../src/dsp/oscillator/SineOscillatorProcessor.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include <span>
#include <functional>

int main() {
    std::cout << "--- ALSA Driver Check ---" << std::endl;

    const int sample_rate = 48000;
    const int block_size = 512;
    const int num_channels = 2; // Stereo hardware target

    auto driver = std::make_unique<hal::AlsaDriver>(sample_rate, block_size, num_channels, "default");
    
    // Create a sine oscillator for testing
    auto sine = std::make_shared<audio::SineOscillatorProcessor>(sample_rate);
    sine->set_frequency(440.0f); // A4

    driver->set_callback([sine, num_channels](std::span<float> output) {
        // AlsaDriver provides a flat span. For interleaved stereo, we need to
        // fill L, R, L, R...
        // SineOscillatorProcessor::pull(span) generates mono samples.
        // We must fill the interleaved buffer correctly.
        for (size_t i = 0; i < output.size(); i += num_channels) {
            float sample = 0.0f;
            sine->pull(std::span<float>(&sample, 1));
            for (int ch = 0; ch < num_channels; ++ch) {
                output[i + ch] = sample;
            }
        }
    });

    std::cout << "Starting ALSA driver (440Hz Sine Wave)..." << std::endl;
    if (!driver->start()) {
        std::cerr << "Failed to start ALSA driver." << std::endl;
        return 1;
    }

    std::cout << "Driver running for 3 seconds..." << std::endl;
    std::cout << "Sample Rate: " << driver->sample_rate() << " Hz" << std::endl;
    std::cout << "Block Size: " << driver->block_size() << " frames" << std::endl;
    std::cout << "Channels: " << driver->channels() << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(3));

    std::cout << "Stopping driver..." << std::endl;
    driver->stop();

    std::cout << "Check complete." << std::endl;

    return 0;
}
