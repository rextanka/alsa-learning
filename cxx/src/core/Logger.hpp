#pragma once

#include <atomic>
#include <string_view>
#include <array>
#include <optional>
#include <cstring>

namespace audio {

/**
 * @brief Represents a single telemetry event.
 * Fixed-size to ensure RT-safety (no allocations).
 */
struct LogEntry {
    enum class Type {
        Message,
        Event
    };

    Type type;
    char tag[32];      // Category or Tag
    float value;       // Numeric value (for Type::Event)
    char message[64];  // Static message (for Type::Message)
    uint64_t timestamp;
};

/**
 * @brief A lock-free, single-producer single-consumer RingBuffer for RT-Safe logging.
 */
template<typename T, size_t Size>
class LockFreeRingBuffer {
public:
    static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");

    bool push(const T& item) {
        size_t h = head.load(std::memory_order_relaxed);
        size_t t = tail.load(std::memory_order_acquire);
        
        if (((h + 1) & mask) == t) {
            return false; // Full
        }
        
        buffer[h] = item;
        head.store((h + 1) & mask, std::memory_order_release);
        return true;
    }

    std::optional<T> pop() {
        size_t t = tail.load(std::memory_order_relaxed);
        size_t h = head.load(std::memory_order_acquire);
        
        if (t == h) {
            return std::nullopt; // Empty
        }
        
        T item = buffer[t];
        tail.store((t + 1) & mask, std::memory_order_release);
        return item;
    }

    bool empty() const {
        return head.load(std::memory_order_acquire) == tail.load(std::memory_order_acquire);
    }

private:
    std::array<T, Size> buffer;
    static constexpr size_t mask = Size - 1;
    std::atomic<size_t> head{0};
    std::atomic<size_t> tail{0};
};

/**
 * @brief Singleton Logger for Audio Thread telemetry.
 */
class AudioLogger {
public:
    static AudioLogger& instance() {
        static AudioLogger inst;
        return inst;
    }

    // Audio Thread Methods (RT-Safe)
    void log_message(const char* tag, const char* msg) {
        LogEntry entry;
        entry.type = LogEntry::Type::Message;
        std::strncpy(entry.tag, tag, sizeof(entry.tag) - 1);
        std::strncpy(entry.message, msg, sizeof(entry.message) - 1);
        entry.timestamp = 0; // TODO: Real timestamp if needed
        ring_buffer.push(entry);
    }

    void log_event(const char* tag, float value) {
        LogEntry entry;
        entry.type = LogEntry::Type::Event;
        std::strncpy(entry.tag, tag, sizeof(entry.tag) - 1);
        entry.value = value;
        entry.timestamp = 0;
        ring_buffer.push(entry);
    }

    // Background Thread Methods
    std::optional<LogEntry> pop_entry() {
        return ring_buffer.pop();
    }

private:
    AudioLogger() = default;
    LockFreeRingBuffer<LogEntry, 1024> ring_buffer;
};

} // namespace audio
