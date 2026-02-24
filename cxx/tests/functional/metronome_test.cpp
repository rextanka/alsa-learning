/**
 * @file metronome_test.cpp
 * @brief Real-time metronome utility using the C-Bridge, AudioDriver, and Logger.
 */

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <cmath>
#include <string>
#include <atomic>
#include <vector>
#include <sstream>
#include "../../include/CInterface.h"
#include "../TestHelper.hpp"
#include "../../src/core/Logger.hpp"

void print_usage() {
    std::cout << "Usage: metronome_test [bpm] [bars] [time_sig_numerator] [--analyze]" << std::endl;
    std::cout << "Defaults: 80 2 4" << std::endl;
    std::cout << "Flags: --analyze/-v : Enable high-precision timing logs" << std::endl;
    std::cout << "Example: ./metronome_test 120 4 3 --analyze" << std::endl;
}

int main(int argc, char* argv[]) {
    // 1. Unified Logging Setup
    audio::AudioLogger::instance().set_log_to_console(true);

    // 2. Command-Line Argument Handling & Defaults
    double bpm = 80.0;
    int total_bars = 2;
    int time_sig_num = 4;
    bool analyze = false;

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        }
        if (arg == "--analyze" || arg == "-v") {
            analyze = true;
        } else {
            args.push_back(arg);
        }
    }

    try {
        if (args.size() > 0) bpm = std::stod(args[0]);
        if (args.size() > 1) total_bars = std::stoi(args[1]);
        if (args.size() > 2) time_sig_num = std::stoi(args[2]);
    } catch (...) {
        LOG_ERROR("Args", "Invalid numeric arguments provided.");
        print_usage();
        return 1;
    }

    if (bpm <= 0 || total_bars <= 0 || time_sig_num <= 0) {
        LOG_ERROR("Args", "All parameters must be positive values.");
        print_usage();
        return 1;
    }

    // 3. Hardware Initialization & Metadata Logging
    test::init_test_environment();
    
    int sample_rate = host_get_device_sample_rate(0);
    if (sample_rate <= 0) sample_rate = 44100;

    double seconds_per_beat = 60.0 / bpm;
    double total_duration = seconds_per_beat * time_sig_num * total_bars;

    {
        std::stringstream ss;
        ss << "Sample Rate: " << sample_rate << " Hz, BPM: " << bpm 
           << ", Meter: " << time_sig_num << "/4, Duration: " << total_duration << "s";
        LOG_INFO("Metronome", ss.str().c_str());
    }

    EngineHandle engine = engine_create(static_cast<unsigned int>(sample_rate));
    if (!engine) {
        LOG_ERROR("Engine", "Failed to create engine.");
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
        std::copy(buffer.left.begin(), buffer.left.end(), buffer.right.begin());
    });

    if (!driver->start()) {
        LOG_ERROR("Driver", "Failed to start audio driver.");
        engine_destroy(engine);
        return 1;
    }

    // 4. Rhythmic Logic & Precision Polling
    std::atomic<bool> polling_running{true};
    int last_beat = -1;
    
    std::thread polling_thread([&]() {
        while (polling_running) {
            int bar, beat, tick;
            engine_get_musical_time(engine, &bar, &beat, &tick);
            
            if (beat != last_beat) {
                std::string note_name = (beat == 1) ? "C4" : "A3";
                
                if (analyze) {
                    auto now = std::chrono::high_resolution_clock::now();
                    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
                    std::stringstream ss;
                    ss << "[ANALYSIS] Beat " << beat << " Triggered at " << micros << "us";
                    LOG_INFO("BeatTrigger", ss.str().c_str());
                } else {
                    std::stringstream ss;
                    ss << "[Beat " << beat << "] Triggering " << note_name << " (Bar " << bar << ")";
                    LOG_INFO("BeatTrigger", ss.str().c_str());
                }
                
                engine_note_on_name(engine, note_name.c_str(), 0.8f);
                
                // 50ms Gate Management
                std::thread([engine, note_name]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    engine_note_off_name(engine, note_name.c_str());
                }).detach();

                last_beat = beat;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    // 5. Synchronous Wait
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(total_duration * 1000)));

    // Cleanup
    polling_running = false;
    if (polling_thread.joinable()) polling_thread.join();

    test::cleanup_test_environment(driver.get());
    engine_destroy(engine);
    
    LOG_INFO("Metronome", "Finished.");
    audio::AudioLogger::instance().flush();

    return 0;
}
