#include <gtest/gtest.h>
#include "MusicalClock.hpp"

using namespace audio;

TEST(ClockTest, PPQResolution) {
    double sample_rate = 48000.0;
    double bpm = 120.0;
    MusicalClock clock(sample_rate, bpm);

    // 120 BPM = 2 beats per second
    // 960 PPQ = 1920 ticks per second
    // 48000 / 1920 = 25 samples per tick
    
    clock.advance(25);
    EXPECT_EQ(clock.total_ticks(), 1);
    
    auto time = clock.current_time();
    EXPECT_EQ(time.bar, 1);
    EXPECT_EQ(time.beat, 1);
    EXPECT_EQ(time.tick, 1);
}

TEST(ClockTest, BarBoundary) {
    double sample_rate = 44100.0;
    double bpm = 60.0; // 1 beat per second
    MusicalClock clock(sample_rate, bpm);
    clock.set_meter(4);

    // 60 BPM, 960 PPQ = 960 ticks per second
    // 44100 / 960 = 45.9375 samples per tick
    
    // Advance 4 beats (1 bar)
    // Total ticks = 4 * 960 = 3840
    // Total samples = 3840 * (44100 / 960) = 4 * 44100 = 176400
    
    clock.advance(176400);
    
    auto time = clock.current_time();
    EXPECT_EQ(time.bar, 2);
    EXPECT_EQ(time.beat, 1);
    EXPECT_EQ(time.tick, 0);
}

TEST(ClockTest, TempoRamp) {
    MusicalClock clock(44100.0, 120.0);
    
    clock.advance(44100); // 1 second at 120 BPM = 2 beats = 1920 ticks
    EXPECT_EQ(clock.total_ticks(), 1920);
    
    clock.set_bpm(60.0);
    clock.advance(44100); // 1 second at 60 BPM = 1 beat = 960 ticks
    EXPECT_EQ(clock.total_ticks(), 1920 + 960);
}
