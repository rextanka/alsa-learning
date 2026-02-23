#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>
#include <memory>
#include "../../cxx/include/CInterface.h"

#ifdef __APPLE__
#include "../hal/coreaudio/CoreAudioDriver.hpp"
using NativeDriver = hal::CoreAudioDriver;
#elif defined(__linux__)
#include "../src/hal/alsa/AlsaDriver.hpp"
#include <functional>
#include <span>
using NativeDriver = hal::AlsaDriver;
#endif

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
    std::cout << "--- Audible Engine Test (5 Seconds) ---" << std::endl;
    
    unsigned int sample_rate = 44100;
    auto driver = std::make_unique<NativeDriver>(sample_rate, 512);
    EngineHandle engine = engine_create(sample_rate);
    
    engine_note_on_name(engine, "C4", 1.0f); // Play a continuous C4
    
    driver->set_callback([engine](std::span<float> output) {
        engine_process(engine, output.data(), output.size());
    });
    
    if (!driver->start()) {
        std::cerr << "Failed to start audio driver!" << std::endl;
        return 1;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(5);
    
    while (std::chrono::steady_clock::now() < end_time) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    driver->stop();
    engine_note_off(engine, 60); // C4
    engine_destroy(engine);
    
    std::cout << "--- Test Finished ---" << std::endl;
    return 0;
}
