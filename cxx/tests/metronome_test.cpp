/**
 * @file metronome_test.cpp
 * @brief Simple validation of MusicalClock and PulseOscillator.
 */

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include "TestHelper.hpp"
#include "../src/core/MusicalClock.hpp"
#include "../src/dsp/oscillator/PulseOscillatorProcessor.hpp"

int main() {
    std::cout << "--- Starting Metronome Validation Test ---" << std::endl;

    test::init_test_environment();
    auto driver = test::create_driver();
    if (!driver) return 1;

    int sample_rate = driver->sample_rate();
    audio::MusicalClock clock(static_cast<double>(sample_rate));
    clock.set_bpm(120.0);
    clock.set_meter(4);

    audio::PulseOscillatorProcessor click(sample_rate);
    click.set_frequency(880.0);
    click.set_pulse_width(0.1f);

    driver->set_callback([&clock, &click, sample_rate](std::span<float> output) {
        int32_t frames = static_cast<int32_t>(output.size());
        
        auto old_time = clock.current_time();
        clock.advance(frames);
        auto new_time = clock.current_time();

        bool is_beat = (new_time.beat != old_time.beat) || (new_time.bar != old_time.bar);
        
        if (is_beat) {
            click.reset(); // Restart pulse
            std::cout << "[Beat] " << new_time.bar << "." << new_time.beat << std::endl;
        }

        click.pull(output);

        // Simple envelope: only play for the first 50ms of a beat
        static int32_t samples_since_beat = 10000; 
        if (is_beat) samples_since_beat = 0;
        
        for (float& sample : output) {
            if (samples_since_beat > (sample_rate * 0.05)) {
                sample = 0.0f;
            }
            samples_since_beat++;
        }
    });

    if (!driver->start()) {
        return 1;
    }

    std::cout << "Playing 120 BPM metronome for 5 seconds (Ctrl+C to stop early)..." << std::endl;
    test::wait_while_running(5);

    test::cleanup_test_environment(driver.get());
    std::cout << "--- Test Completed ---" << std::endl;

    return 0;
}
