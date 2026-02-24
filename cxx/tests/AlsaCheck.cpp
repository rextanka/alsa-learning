/**
 * @file AlsaCheck.cpp
 * @brief Utility to verify ALSA driver and audio processing.
 */

#include "../src/hal/alsa/AlsaDriver.hpp"
#include "../src/dsp/oscillator/SineOscillatorProcessor.hpp"
#include "../src/dsp/routing/MonoToStereoProcessor.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include <span>
#include <functional>
#include <vector>

int main() {
    std::cout << "--- ALSA Driver Check ---" << std::endl;

    const int sample_rate = 48000;
    const int block_size = 512;
    const int num_channels = 2; // Stereo hardware target

    auto driver = std::make_unique<hal::AlsaDriver>(sample_rate, block_size, num_channels, "default");
    
    // Create a sine oscillator for testing
    auto sine = std::make_shared<audio::SineOscillatorProcessor>(sample_rate);
    sine->set_frequency(440.0f); // A4

    // RT-Safe: Pre-allocate mono buffer outside the callback to avoid heap allocations
    // while the audio driver is running.
    auto mono_buffer = std::make_shared<std::vector<float>>(block_size);

    // RT-Safe callback: No heap allocations or hidden gaps.
    driver->set_interleaved_callback([sine, mono_buffer](std::span<float> output) {
        // AlsaDriver block_size is 512, stereo output is 1024.
        // mono_buffer is also 512.
        std::span<float> mono_span(mono_buffer->data(), mono_buffer->size());

        // Fill exactly 512 mono samples
        sine->pull(mono_span);

        // Interleave exactly 512 mono samples into 1024 stereo samples.
        // This MUST cover the full output span to avoid clicking gaps.
        audio::MonoToStereoProcessor::process(mono_span, output);
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
