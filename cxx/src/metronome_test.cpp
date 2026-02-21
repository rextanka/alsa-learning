#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>
#include <memory>
#include "include/CInterface.h"
#include "audio/RingBufferLogger.hpp"

#ifdef __APPLE__
#include "hal/coreaudio/CoreAudioDriver.hpp"
using NativeDriver = hal::CoreAudioDriver;
#elif defined(__linux__)
#include "hal/alsa/AlsaDriver.hpp"
using NativeDriver = hal::AlsaDriver;
#endif

std::string format_time(std::chrono::system_clock::time_point tp) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
    auto timer = std::chrono::system_clock::to_time_t(tp);
    std::tm bt = *std::localtime(&timer);
    std::ostringstream oss;
    oss << std::put_time(&bt, "%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

int main() {
    std::cout << "--- Audible Metronome Test (5 Seconds) ---" << std::endl;
    std::cout << "Cadence: C4 (High) on Beat 1, A4 (Low) on Beats 2, 3, 4." << std::endl;
    
    unsigned int sample_rate = 44100;
    auto driver = std::make_unique<NativeDriver>(sample_rate, 512);
    EngineHandle engine = engine_create(sample_rate);
    
    engine_set_bpm(engine, 120.0);
    
    // We'll use the C-API engine for the metronome.
    // It uses VoiceManager which already has voices with Sine + ADSR (close enough to AD).
    
    int last_beat = -1;
    
    driver->set_callback([engine, &last_beat](std::span<float> output) {
        engine_process(engine, output.data(), output.size());
        
        int bar, beat, tick;
        engine_get_musical_time(engine, &bar, &beat, &tick);
        
        if (beat != last_beat) {
            // Trigger metronome sound
            if (beat == 1) {
                // High tick: C5 (actually let's do C5 for high, C4 for low)
                engine_note_on_name(engine, "C5", 0.8f);
            } else {
                // Low tick
                engine_note_on_name(engine, "C4", 0.6f);
            }
            
            // For a metronome, we want a very short 'blip'. 
            // We trigger note off almost immediately after (next process call or next few ticks).
            // For this test, let's just trigger note_off right away; the ADSR release will handle the decay.
            if (beat == 1) engine_note_off(engine, 72); // C5
            else engine_note_off(engine, 60); // C4
            
            // Log via thread-safe logger
            char log_msg[64];
            snprintf(log_msg, 64, "TICK (Bar: %d, Beat: %d)", bar, beat);
            audio::RingBufferLogger::instance().log(log_msg);
            
            last_beat = beat;
        }
        
        // We should release the notes shortly after trigger. 
        // A simple way is to check ticks, but for this test, we'll just note_off the previous note
        // when the next one starts, or use a timer.
        // For simplicity here, let's just trigger a note_off 100ms later? 
        // Actually, let's just let the ADSR release naturally if it's short.
    });
    
    if (!driver->start()) {
        std::cerr << "Failed to start audio driver!" << std::endl;
        return 1;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(5);
    
    while (std::chrono::steady_clock::now() < end_time) {
        // Spool logs from ring buffer to console
        audio::RingBufferLogger::LogEntry entry;
        while (audio::RingBufferLogger::instance().try_pop(entry)) {
            std::cout << "[" << format_time(entry.timestamp) << "] " << entry.message << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    driver->stop();
    engine_destroy(engine);
    
    std::cout << "--- Metronome Test Finished ---" << std::endl;
    return 0;
}
