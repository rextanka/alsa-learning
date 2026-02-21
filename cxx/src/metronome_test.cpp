#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>
#include "include/CInterface.h"

// For testing internal state
#include "../audio/VoiceManager.hpp"
#include "../audio/MusicalClock.hpp"
#include "../audio/TuningSystem.hpp"
struct InternalEngine {
    std::unique_ptr<audio::VoiceManager> voice_manager;
    audio::MusicalClock clock;
    audio::TwelveToneEqual tuning;
};

std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto timer = std::chrono::system_clock::to_time_t(now);
    std::tm bt = *std::localtime(&timer);
    
    std::ostringstream oss;
    oss << std::put_time(&bt, "%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

int main() {
    std::cout << "--- Metronome Console Test (5 Seconds) ---" << std::endl;
    
    unsigned int sample_rate = 44100;
    EngineHandle handle = engine_create(sample_rate);
    engine_set_bpm(handle, 120.0); // 2 beats per second
    
    int last_beat = -1;
    float dummy_buffer[512];
    
    // Simulating audio heartbeat precisely for 5 seconds worth of samples
    // 5 seconds @ 44100Hz = 220500 samples
    // 220500 / 512 = ~430 blocks
    size_t total_samples = sample_rate * 5;
    size_t samples_processed = 0;
    
    while (samples_processed < total_samples) {
        engine_process(handle, dummy_buffer, 512);
        samples_processed += 512;
        
        int bar, beat, tick;
        engine_get_musical_time(handle, &bar, &beat, &tick);
        
        if (beat != last_beat) {
            std::cout << "[" << get_timestamp() << "] TICK (Bar: " << bar << ", Beat: " << beat << ")" << std::endl;
            last_beat = beat;
        }
        
        // Simulating the passing of time so we don't dump everything instantly
        std::this_thread::sleep_for(std::chrono::milliseconds(512 * 1000 / sample_rate));
    }
    
    engine_destroy(handle);
    std::cout << "--- Metronome Test Finished ---" << std::endl;
    return 0;
}
