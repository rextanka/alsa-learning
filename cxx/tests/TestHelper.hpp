/**
 * @file TestHelper.hpp
 * @brief Utilities for platform-agnostic audio testing and signal handling.
 */

#ifndef TEST_HELPER_HPP
#define TEST_HELPER_HPP

#include <iostream>
#include <memory>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include "../src/hal/AudioDriver.hpp"
#include "../src/core/AudioSettings.hpp"
#include "../src/core/Logger.hpp"

#ifdef __APPLE__
#include "../src/hal/coreaudio/CoreAudioDriver.hpp"
#elif defined(__linux__)
#include "../src/hal/alsa/AlsaDriver.hpp"
#endif

namespace test {

/**
 * @brief Global flag for signal handling.
 */
inline std::atomic<bool> g_keep_running{true};

/**
 * @brief Simple signal handler for SIGINT.
 */
inline void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\n[SIGINT] Stopping audio..." << std::endl;
        g_keep_running = false;
    }
}

/**
 * @brief Initialize signal handling and audio settings.
 */
inline void init_test_environment() {
    std::signal(SIGINT, signal_handler);
    
    // Fedora/Razer default settings
#ifdef __linux__
    auto& settings = audio::AudioSettings::instance();
    settings.sample_rate = 48000;
    settings.block_size = 512;
#endif
}

/**
 * @brief Factory function to create the correct native audio driver.
 */
inline std::unique_ptr<hal::AudioDriver> create_driver() {
    auto& settings = audio::AudioSettings::instance();
    int sr = settings.sample_rate.load();
    int bs = settings.block_size.load();

#ifdef __APPLE__
    return std::make_unique<hal::CoreAudioDriver>(sr, bs);
#elif defined(__linux__)
    return std::make_unique<hal::AlsaDriver>(sr, bs, 2, "default");
#else
    std::cerr << "Error: No audio driver supported on this platform." << std::endl;
    return nullptr;
#endif
}

/**
 * @brief Wait while audio is running, handling the keep_running flag.
 */
inline void wait_while_running(int seconds = -1) {
    auto start_time = std::chrono::steady_clock::now();
    while (g_keep_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        if (seconds > 0) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() >= seconds) {
                break;
            }
        }
    }
}

/**
 * @brief Cleanup test environment, flush logs, and stop driver.
 */
inline void cleanup_test_environment(hal::AudioDriver* driver) {
    if (driver) {
        driver->stop();
    }
    audio::AudioLogger::instance().flush();
}

} // namespace test

#endif // TEST_HELPER_HPP
