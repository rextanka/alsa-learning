/**
 * @file AlsaCheck.cpp
 * @brief Utility to verify ALSA driver and audio processing.
 */

#include "../src/hal/alsa/AlsaDriver.hpp"
#include "../src/dsp/oscillator/SineOscillatorProcessor.hpp"
#include "../src/dsp/routing/MonoToStereoProcessor.hpp"
#include "../src/core/AudioSettings.hpp"
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
    
    // Create a sine oscillator for testing (initially with requested rate)
    auto sine = std::make_shared<audio::SineOscillatorProcessor>(sample_rate);
    sine->set_frequency(440.0f); // A4

    // --- Pitch Accuracy: Get the hardware-negotiated sample rate before starting if possible.
    // We sync here to avoid mid-stream updates.
    sine->set_sample_rate(sample_rate);

    // RT-Safe: Pre-allocate mono scratch buffer to a safe maximum to avoid runtime resizes.
    // 2048 frames is plenty for most hardware.
    const size_t max_frames = 2048;
    auto mono_buffer = std::make_shared<std::vector<float>>(max_frames, 0.0f);

    // RT-Safe callback: 100% hardware-driven.
    driver->set_interleaved_callback([sine, mono_buffer](std::span<float> output) {
        // The hardware span (output) is the "master truth"
        size_t frames_needed = output.size() / 2;

        // Safety check: ensure our mono scratch buffer is large enough
        if (mono_buffer->size() < frames_needed) return;

        std::span<float> mono_span(mono_buffer->data(), frames_needed);
        sine->pull(mono_span);
        
        // Interleave exactly the number of frames requested by the driver.
        audio::MonoToStereoProcessor::process(mono_span, output);
    });

    std::cout << "Starting ALSA driver (440Hz Sine Wave)..." << std::endl;
    if (!driver->start()) {
        std::cerr << "Failed to start ALSA driver." << std::endl;
        return 1;
    }

    // Dynamic Stabilization: Audit hardware parameters
    const int actual_rate = audio::AudioSettings::instance().sample_rate;
    const int actual_block = audio::AudioSettings::instance().block_size;
    
    // Audit check: If the hardware rate changed, we update only once here.
    if (actual_rate != sample_rate) {
        sine->set_sample_rate(actual_rate);
    }

    std::cout << "Driver running for 3 seconds..." << std::endl;
    std::cout << "Actual Sample Rate: " << actual_rate << " Hz" << std::endl;
    std::cout << "Actual Block Size: " << actual_block << " frames" << std::endl;
    std::cout << "Channels: " << driver->channels() << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(3));

    std::cout << "Stopping driver..." << std::endl;
    driver->stop();

    std::cout << "Check complete." << std::endl;

    return 0;
}
