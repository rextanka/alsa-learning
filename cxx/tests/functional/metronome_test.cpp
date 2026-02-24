/**
 * @file metronome_test.cpp
 * @brief Real-time metronome utility using the C-Bridge and AudioDriver.
 */

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <cmath>
#include <string>
#include <atomic>
#include <vector>
#include "../../include/CInterface.h"
#include "../TestHelper.hpp"

void print_usage() {
    std::cout << "Usage: metronome_test [bpm] [bars] [time_signature_numerator]" << std::endl;
    std::cout << "Defaults: bpm=80, bars=2, time_sig=4" << std::endl;
    std::cout << "Example: ./metronome_test 120 4 3 (120 BPM, 4 bars, 3/4 time)" << std::endl;
}

int main(int argc, char* argv[]) {
    // 1. Command-Line Argument Handling & Defaults
    double bpm = 80.0;
    int total_bars = 2;
    int time_sig_num = 4;

    std::vector<std::string> args(argv, argv + argc);
    for (const auto& arg : args) {
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        }
    }

    try {
        if (argc > 1) {
            bpm = std::stod(argv[1]);
        }
        if (argc > 2) {
            total_bars = std::stoi(argv[2]);
        }
        if (argc > 3) {
            time_sig_num = std::stoi(argv[3]);
        }
    } catch (...) {
        std::cerr << "Error: Invalid arguments." << std::endl;
        print_usage();
        return 1;
    }

    if (bpm <= 0 || total_bars <= 0 || time_sig_num <= 0) {
        std::cerr << "Error: All parameters must be positive values." << std::endl;
        print_usage();
        return 1;
    }

    std::cout << "--- Real-Time Metronome ---" << std::endl;
    std::cout << "BPM: " << bpm << ", Bars: " << total_bars << ", Time Signature: " << time_sig_num << "/4" << std::endl;

    // 2. Real-Time Audio Integration
    test::init_test_environment();
    
    // Get hardware sample rate via bridge call (Phase 15 implementation)
    int sample_rate = host_get_device_sample_rate(0);
    if (sample_rate <= 0) {
        sample_rate = 44100; // Fallback
    }
    std::cout << "Using Hardware Sample Rate: " << sample_rate << " Hz" << std::endl;

    EngineHandle engine = engine_create(static_cast<unsigned int>(sample_rate));
    if (!engine) {
        std::cerr << "Failed to create audio engine." << std::endl;
        return 1;
    }

    engine_set_bpm(engine, bpm);
    engine_set_meter(engine, time_sig_num);

    auto driver = test::create_driver();
    if (!driver) {
        engine_destroy(engine);
        return 1;
    }

    driver->set_stereo_callback([engine](audio::AudioBuffer& buffer) {
        engine_process(engine, buffer.left.data(), buffer.left.size());
        // Copy mono result to right for stereo output
        std::copy(buffer.left.begin(), buffer.left.end(), buffer.right.begin());
    });

    if (!driver->start()) {
        std::cerr << "Failed to start audio driver." << std::endl;
        engine_destroy(engine);
        return 1;
    }

    // 3. Rhythmic Logic & Polling Thread
    std::atomic<bool> running{true};
    int last_beat = -1;
    
    std::thread polling_thread([&]() {
        while (running) {
            int bar, beat, tick;
            engine_get_musical_time(engine, &bar, &beat, &tick);
            
            if (beat != last_beat) {
                if (beat == 1) {
                    std::cout << "[Beat " << beat << "] Triggering C4 (Downbeat)" << std::endl;
                    engine_note_on_name(engine, "C4", 0.8f);
                } else {
                    std::cout << "[Beat " << beat << "] Triggering A3" << std::endl;
                    engine_note_on_name(engine, "A3", 0.8f);
                }
                
                // 50ms Gate Management
                std::thread([engine, beat]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    if (beat == 1) {
                        engine_note_off_name(engine, "C4");
                    } else {
                        engine_note_off_name(engine, "A3");
                    }
                }).detach();

                last_beat = beat;
            }
            
            // High-frequency polling
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    // 4. Synchronous Wait
    double seconds_per_beat = 60.0 / bpm;
    double total_duration = seconds_per_beat * time_sig_num * total_bars;
    
    std::cout << "Playing for " << total_duration << " seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(total_duration * 1000)));

    // Cleanup
    running = false;
    if (polling_thread.joinable()) {
        polling_thread.join();
    }

    test::cleanup_test_environment(driver.get());
    engine_destroy(engine);
    
    std::cout << "--- Metronome Finished ---" << std::endl;

    return 0;
}
