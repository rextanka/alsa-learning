#pragma once

#include <cstdint>
#include <cmath>
#include <cstdio>

namespace audio {

/**
 * @brief Represents a point in musical time.
 */
struct MusicalTime {
    int32_t bar;    ///< 1-based bar number
    int32_t beat;   ///< 1-based beat number (within bar)
    int32_t tick;   ///< 0-based tick number (within beat)

    bool operator==(const MusicalTime& other) const {
        return bar == other.bar && beat == other.beat && tick == other.tick;
    }
};

/**
 * @brief Tracks musical time based on sample-accurate pulse.
 * 
 * Uses 960 PPQ (Pulses Per Quarter-note) for high resolution.
 */
class MusicalClock {
public:
    static constexpr int32_t PPQ = 960;

    MusicalClock(double sample_rate, double bpm = 120.0)
        : sample_rate_(sample_rate)
        , bpm_(bpm)
        , beats_per_bar_(4)
    {
        update_tick_duration();
    }

    void set_bpm(double bpm) {
        // Sync total_ticks before changing tempo
        total_ticks_base_ = total_ticks_;
        total_samples_base_ = total_samples_;
        bpm_ = bpm;
        update_tick_duration();
    }

    void set_sample_rate(double sample_rate) {
        // Sync total_ticks before changing sample rate
        total_ticks_base_ = total_ticks_;
        total_samples_base_ = total_samples_;
        sample_rate_ = sample_rate;
        update_tick_duration();
    }

    void set_meter(int32_t beats_per_bar) {
        beats_per_bar_ = beats_per_bar;
        update_tick_duration();
    }

    /**
     * @brief Advance the clock by a number of samples.
     */
    void advance(int32_t num_samples) {
        total_samples_ += static_cast<double>(num_samples);
        
        // Calculate ticks since the last tempo/rate change
        double samples_since_base = total_samples_ - total_samples_base_;
        int64_t ticks_since_base = static_cast<int64_t>(std::floor(samples_since_base / samples_per_tick_));
        
        total_ticks_ = total_ticks_base_ + ticks_since_base;
    }

    MusicalTime current_time() const {
        int64_t remaining_ticks = total_ticks_;
        
        int32_t bar = static_cast<int32_t>(remaining_ticks / (PPQ * beats_per_bar_)) + 1;
        remaining_ticks %= (PPQ * beats_per_bar_);
        
        int32_t beat = static_cast<int32_t>(remaining_ticks / PPQ) + 1;
        int32_t tick = static_cast<int32_t>(remaining_ticks % PPQ);
        
        return {bar, beat, tick};
    }

    int32_t beats_per_bar() const { return beats_per_bar_; }
    double bpm() const { return bpm_; }
    int64_t total_ticks() const { return total_ticks_; }

private:
    void update_tick_duration() {
        // ticks per second = (bpm / 60) * PPQ
        // samples per tick = sample_rate / ticks_per_second
        double ticks_per_second = (bpm_ / 60.0) * static_cast<double>(PPQ);
        samples_per_tick_ = sample_rate_ / ticks_per_second;
    }

    double sample_rate_;
    double bpm_;
    int32_t beats_per_bar_;
    
    double samples_per_tick_ = 0;
    double total_samples_ = 0;       // Continuous high precision sample counter
    double total_samples_base_ = 0;  // Sample count at last tempo/rate change
    int64_t total_ticks_ = 0;        // Current total ticks
    int64_t total_ticks_base_ = 0;   // Ticks at last tempo/rate change
};

} // namespace audio
