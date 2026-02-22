#include <gtest/gtest.h>
#include "Logger.hpp"
#include <thread>
#include <chrono>
#include <vector>
#include <string>

using namespace audio;

TEST(LoggerTest, SingleThreadedPushPop) {
    auto& logger = AudioLogger::instance();
    
    // Clear any existing entries
    while (logger.pop_entry()) {}

    logger.log_message("TEST", "Hello World");
    logger.log_event("VALUE", 42.0f);

    auto entry1 = logger.pop_entry();
    ASSERT_TRUE(entry1.has_value());
    EXPECT_EQ(entry1->type, LogEntry::Type::Message);
    EXPECT_STREQ(entry1->tag, "TEST");
    EXPECT_STREQ(entry1->message, "Hello World");

    auto entry2 = logger.pop_entry();
    ASSERT_TRUE(entry2.has_value());
    EXPECT_EQ(entry2->type, LogEntry::Type::Event);
    EXPECT_STREQ(entry2->tag, "VALUE");
    EXPECT_EQ(entry2->value, 42.0f);

    EXPECT_FALSE(logger.pop_entry().has_value());
}

TEST(LoggerTest, MultiThreadedCapture) {
    auto& logger = AudioLogger::instance();
    while (logger.pop_entry()) {}

    std::atomic<bool> running{true};
    std::vector<LogEntry> captured;

    // "Background" thread (Consumer)
    std::thread consumer([&]() {
        while (running || !logger.pop_entry() == false) {
            if (auto entry = logger.pop_entry()) {
                captured.push_back(*entry);
            } else {
                std::this_thread::yield();
            }
        }
    });

    // "Audio" thread (Producer)
    std::thread producer([&]() {
        for (int i = 0; i < 100; ++i) {
            logger.log_event("ITER", static_cast<float>(i));
        }
    });

    producer.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    running = false;
    consumer.join();

    EXPECT_EQ(captured.size(), 100);
    if (!captured.empty()) {
        EXPECT_STREQ(captured[0].tag, "ITER");
        EXPECT_EQ(captured[0].value, 0.0f);
        EXPECT_EQ(captured.back().value, 99.0f);
    }
}
