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

    driver->set_interleaved_callback([sine, mono_buffer](std::span<float> output) {
        // 1. Pull mono samples from oscillator into pre-allocated scratch buffer
        std::span<float> mono_span(*mono_buffer);
        sine->pull(mono_span);

        // 2. Interleave mono to stereo using the formal processor node
        // This ensures the 512-frame mono oscillator fills the 1024-sample stereo driver span correctly.
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
