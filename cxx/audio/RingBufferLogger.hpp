#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>

namespace audio {

/**
 * @brief Opaque fixed-size ring buffer for lock-free logging from audio thread.
 */
class RingBufferLogger {
public:
    struct LogEntry {
        char message[64];
        std::chrono::system_clock::time_point timestamp;
    };

    static RingBufferLogger& instance() {
        static RingBufferLogger logger;
        return logger;
    }

    void log(const char* msg) {
        size_t next = (write_pos_.load(std::memory_order_relaxed) + 1) % MAX_ENTRIES;
        if (next == read_pos_.load(std::memory_order_acquire)) {
            return; // Buffer full
        }

        LogEntry& entry = buffer_[write_pos_.load(std::memory_order_relaxed)];
        size_t i = 0;
        while (i < 63 && msg[i] != '\0') {
            entry.message[i] = msg[i];
            i++;
        }
        entry.message[i] = '\0';
        entry.timestamp = std::chrono::system_clock::now();

        write_pos_.store(next, std::memory_order_release);
    }

    bool try_pop(LogEntry& out_entry) {
        size_t current_read = read_pos_.load(std::memory_order_relaxed);
        if (current_read == write_pos_.load(std::memory_order_acquire)) {
            return false;
        }

        out_entry = buffer_[current_read];
        read_pos_.store((current_read + 1) % MAX_ENTRIES, std::memory_order_release);
        return true;
    }

private:
    RingBufferLogger() : write_pos_(0), read_pos_(0) {
        buffer_.resize(MAX_ENTRIES);
    }

    static constexpr size_t MAX_ENTRIES = 1024;
    std::vector<LogEntry> buffer_;
    std::atomic<size_t> write_pos_;
    std::atomic<size_t> read_pos_;
};

} // namespace audio
