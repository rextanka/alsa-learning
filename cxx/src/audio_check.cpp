#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include "include/CInterface.h"

#ifdef __APPLE__
#include "hal/coreaudio/CoreAudioDriver.hpp"
using NativeDriver = hal::CoreAudioDriver;
#elif defined(__linux__)
#include "hal/alsa/AlsaDriver.hpp"
using NativeDriver = hal::AlsaDriver;
#endif

int main() {
    std::cout << "--- Audible Verification: Playing A4 (440Hz) for 1 Second ---" << std::endl;
    
    unsigned int sample_rate = 44100;
    auto driver = std::make_unique<NativeDriver>(sample_rate, 512);
    EngineHandle engine = engine_create(sample_rate);
    
    // Set a simple ADSR to ensure we hear the onset
    // We don't have direct engine-level ADSR access yet in C API easily for all voices,
    // but the default voice setup in VoiceManager has one.
    
    driver->set_callback([engine](std::span<float> output) {
        engine_process(engine, output.data(), output.size());
    });
    
    if (!driver->start()) {
        std::cerr << "Failed to start audio driver!" << std::endl;
        return 1;
    }
    
    std::cout << "Triggering 'A4'..." << std::endl;
    engine_note_on_name(engine, "A4", 0.8f);
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    std::cout << "Releasing..." << std::endl;
    // We need a way to release by name or just release all. 
    // For now, we'll assume A4 is MIDI 69.
    engine_note_off(engine, 69);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    driver->stop();
    engine_destroy(engine);
    
    std::cout << "--- Test Complete ---" << std::endl;
    return 0;
}
