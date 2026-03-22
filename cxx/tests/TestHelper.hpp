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
#include <iomanip>
#include "../../include/CInterface.h"

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
 * @brief Initialize signal handling.
 */
inline void init_test_environment() {
    std::signal(SIGINT, signal_handler);
}

/**
 * @brief Query the hardware for the best sample rate with a safe fallback.
 */
inline int get_safe_sample_rate(int device_index = 0) {
    int rate = host_get_device_sample_rate(device_index);
    if (rate <= 0) {
        std::cerr << "Warning: Could not query hardware sample rate. Falling back to 44100Hz." << std::endl;
        return 44100;
    }
    return rate;
}

/**
 * @brief RAII wrapper for EngineHandle to ensure cleanup.
 */
class EngineWrapper {
public:
    explicit EngineWrapper(int sample_rate) {
        handle = engine_create(sample_rate);
    }
    ~EngineWrapper() {
        if (handle) {
            engine_destroy(handle);
        }
    }
    operator EngineHandle() const { return handle; }
    EngineHandle get() const { return handle; }

private:
    EngineHandle handle;
};

/**
 * @brief Start audio engine and wait for hardware to settle.
 *
 * On macOS and Linux, the first audio callback may fire before the first
 * note_on if the engine is started immediately before playing. A short
 * warmup prevents the first note being dropped (the "3 hits not 4" problem).
 * Returns the engine_start() return value for ASSERT_EQ checks.
 */
inline int engine_start_warmup(EngineHandle h, int warmup_ms = 500) {
    int rc = engine_start(h);
    if (rc == 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(warmup_ms));
    return rc;
}

#define ASSERT_ENGINE_START(h) ASSERT_EQ(test::engine_start_warmup(h), 0)

/**
 * @brief Print standardized test header.
 */
inline void print_test_header(const char* test_name, 
                             const char* intent, 
                             const char* signal_chain, 
                             const char* expected, 
                             int sample_rate) {
    double latency_ms = (512.0 / static_cast<double>(sample_rate)) * 1000.0;
    
    std::cout << "================================================================" << std::endl;
    std::cout << "--- TEST: " << test_name << " ---" << std::endl;
    std::cout << "Intent:   " << intent << std::endl;
    std::cout << "Chain:    " << signal_chain << std::endl;
    std::cout << "Expected: " << expected << std::endl;
    std::cout << "Hardware: " << sample_rate << "Hz | ~"
              << std::fixed << std::setprecision(1) << latency_ms << "ms latency (512 samples)" << std::endl;
    std::cout << "================================================================" << std::endl;
    // Reset stream formatting so callers see default float output, not fixed/1dp.
    std::cout << std::defaultfloat << std::setprecision(6);
}

#define PRINT_TEST_HEADER(name, intent, chain, expected, rate) \
    test::print_test_header(name, intent, chain, expected, rate)

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

} // namespace test

#endif // TEST_HELPER_HPP
