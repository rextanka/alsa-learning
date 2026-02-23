#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>
#include <memory>
#include <numeric> // For std::accumulate
#include <algorithm> // For std::max
#include <cmath>

#include "../../cxx/include/CInterface.h"

#ifdef __APPLE__
#include "../hal/coreaudio/CoreAudioDriver.hpp"
using NativeDriver = hal::CoreAudioDriver;
#elif defined(__linux__)
#include <functional>
#include <span>
#include "../src/hal/alsa/AlsaDriver.hpp"
using NativeDriver = hal::AlsaDriver;
#endif

namespace {
    // MIDI Note to Frequency conversion (A4 = 440 Hz)
    double midi_note_to_frequency(int midi_note) {
        return 440.0 * std::pow(2.0, (midi_note - 69) / 12.0);
    }
}

// A simple structure to hold log entries
struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    std::string message;
};

std::string format_time(std::chrono::system_clock::time_point tp) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
    auto timer = std::chrono::system_clock::to_time_t(tp);
    std::tm bt = *std::localtime(&timer);
    std::ostringstream oss;
    oss << std::put_time(&bt, "%H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

int main() {
    std::cout << "--- Audible Metronome Test (16 Bars at 80 BPM) ---" << std::endl;
    std::cout << "Cadence: C4 on Beat 1, A3 on Beats 2, 3, 4." << std::endl;
    
    unsigned int sample_rate = 44100;
    unsigned int buffer_size = 512; // Typical buffer size

    auto driver = std::make_unique<NativeDriver>(sample_rate, buffer_size);
    EngineHandle engine = engine_create(sample_rate);
    
    // Set BPM to 80
    engine_set_bpm(engine, 80.0);
    
    // Set ADSR parameters (Attack: 5ms, Decay: 50ms, Sustain: 0.0, Release: 50ms)
    engine_set_adsr(engine, 0.005f, 0.05f, 0.0f, 0.05f);

    // Set meter explicitly
    engine_set_meter(engine, 4);

    long long current_sample_frame = 0;
    int last_beat = -1;
    float peak_amplitude = 0.0f;

    // Reserve space for logs
    std::vector<LogEntry> logs;
    logs.reserve(128); 

    // Sine wave parameters for bypass
    double sine_phase = 0.0;
    double sine_frequency = 440.0; // A4
    double sine_amplitude = 0.05;

    driver->set_callback([&](std::span<float> output) {
        // Sine bypass for the first 1 second
        if (current_sample_frame < (long long)sample_rate) { // 1 second bypass
            for (size_t i = 0; i < output.size(); ++i) {
                output[i] = static_cast<float>(sine_amplitude * std::sin(2.0 * M_PI * sine_frequency * sine_phase / sample_rate));
                sine_phase += 1.0;
                if (sine_phase >= (double)sample_rate) sine_phase -= (double)sample_rate;
            }
        } else {
            engine_process(engine, output.data(), output.size());
            
            int bar, beat, tick;
            engine_get_musical_time(engine, &bar, &beat, &tick);
            
            // Edge-trigger logic based on total ticks processed
            // Note: In real scenarios, we would query the clock directly, 
            // but here we use the engine_get_musical_time which depends on clock.advance().
            
            if (beat != last_beat) {
                // Reset gates
                engine_note_off(engine, 60); // C4
                engine_note_off(engine, 57); // A3

                if (beat == 1) {
                    engine_note_on_name(engine, "C4", 1.0f); // C4 on Beat 1
                } else {
                    engine_note_on_name(engine, "A3", 1.0f); // A3 on Beats 2-4
                }
                char log_msg[128];
                snprintf(log_msg, sizeof(log_msg), "TICK (Bar: %d, Beat: %d, Time: %d:%d:%d)", bar, beat, bar, beat, tick);
                logs.push_back({std::chrono::system_clock::now(), log_msg});
                last_beat = beat;
            }
        }

        // Buffer Probe: Check for peak amplitude
        for (float sample : output) {
            float abs_sample = std::abs(sample);
            if (abs_sample > peak_amplitude) {
                peak_amplitude = abs_sample;
            }
        }

        current_sample_frame += output.size();
    });
    
    if (!driver->start()) {
        std::cerr << "Failed to start audio driver!" << std::endl;
        return 1;
    }
    
    // 16 bars = 16 * 4 beats * (60s/80bpm) = 48 seconds
    double total_duration_seconds = 48.0; 
    long long total_frames = static_cast<long long>(total_duration_seconds * sample_rate);

    while (current_sample_frame < total_frames) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    driver->stop();
    engine_destroy(engine);
    
    std::cout << "--- Metronome Test Finished ---" << std::endl;

    // Post-run logging report
    std::cout << "\nLog Report:" << std::endl;
    for (const auto& entry : logs) {
        std::cout << "[" << format_time(entry.timestamp) << "] " << entry.message << std::endl;
    }
    std::cout << "Peak Amplitude Detected: " << peak_amplitude << std::endl;

    if (peak_amplitude < 0.01f) {
        std::cout << "WARNING: Peak amplitude is very low. Metronome might not be audible." << std::endl;
        return 1;
    }

    return 0;
}
