/**
 * @file PerformanceProfiler.hpp
 * @brief Lightweight performance profiling for audio processors.
 * 
 * This file follows the project rules defined in .cursorrules:
 * - Modern C++: Target C++20/23 for all new code.
 * - Performance: Lightweight, compile-time optional profiling.
 * - Embedded-friendly: Minimal overhead, cycle counter support for embedded systems.
 */

#ifndef PERFORMANCE_PROFILER_HPP
#define PERFORMANCE_PROFILER_HPP

#include <chrono>
#include <cstddef>

namespace audio {

/**
 * @brief Lightweight performance profiler for measuring processor execution time.
 * 
 * Design goals:
 * - Lightweight and minimally invasive
 * - Embedding-friendly (cycle counter support)
 * - Compile-time optional (zero cost when disabled)
 * - Nanosecond precision
 */
class PerformanceProfiler {
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;
    using Nanoseconds = std::chrono::nanoseconds;

    PerformanceProfiler() = default;

#if AUDIO_ENABLE_PROFILING
    /**
     * @brief Start timing measurement.
     */
    void start() {
        start_time_ = Clock::now();
    }

    /**
     * @brief Stop timing measurement and store elapsed time.
     */
    void stop() {
        const auto end_time = Clock::now();
        execution_time_ = std::chrono::duration_cast<Nanoseconds>(end_time - start_time_);
        
        // Update max execution time
        if (execution_time_ > max_execution_time_) {
            max_execution_time_ = execution_time_;
        }
        
        total_blocks_processed_++;
    }

    /**
     * @brief Get the last execution time in nanoseconds.
     */
    Nanoseconds elapsed() const {
        return execution_time_;
    }

    /**
     * @brief Get the maximum execution time observed.
     */
    Nanoseconds max_execution_time() const {
        return max_execution_time_;
    }

    /**
     * @brief Get total number of blocks processed.
     */
    size_t total_blocks_processed() const {
        return total_blocks_processed_;
    }

    /**
     * @brief Check if execution time exceeds the buffer budget.
     * 
     * @param buffer_budget Maximum allowed time in nanoseconds
     * @return true if execution time exceeds budget
     */
    bool exceeds_budget(Nanoseconds buffer_budget) const {
        return execution_time_ > buffer_budget;
    }

    /**
     * @brief Reset all metrics.
     */
    void reset() {
        execution_time_ = Nanoseconds::zero();
        max_execution_time_ = Nanoseconds::zero();
        total_blocks_processed_ = 0;
    }

private:
    TimePoint start_time_;
    Nanoseconds execution_time_{0};
    Nanoseconds max_execution_time_{0};
    size_t total_blocks_processed_{0};

#else
    // When profiling is disabled, all methods are no-ops (zero cost)
    void start() {}
    void stop() {}
    Nanoseconds elapsed() const { return Nanoseconds::zero(); }
    Nanoseconds max_execution_time() const { return Nanoseconds::zero(); }
    size_t total_blocks_processed() const { return 0; }
    bool exceeds_budget(Nanoseconds) const { return false; }
    void reset() {}
#endif
};

} // namespace audio

#endif // PERFORMANCE_PROFILER_HPP
