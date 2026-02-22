#pragma once

#include <cstdint>
#include <cmath>

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
        bpm_ = bpm;
        update_tick_duration();
    }

    void set_sample_rate(double sample_rate) {
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
        samples_acc_ += static_cast<double>(num_samples);
        
        while (samples_acc_ >= samples_per_tick_) {
            samples_acc_ -= samples_per_tick_;
            total_ticks_++;
        }
    }

    MusicalTime current_time() const {
        int64_t remaining_ticks = total_ticks_;
        
        // Use the actual beats_per_bar_ for calculation
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
        printf("[MusicalClock] SR: %.1f, BPM: %.2f, SPT: %.6f\n", sample_rate_, bpm_, samples_per_tick_);
    }

    double sample_rate_;
    double bpm_;
    int32_t beats_per_bar_;
    
    double samples_per_tick_ = 0;
    double samples_acc_ = 0;
    int64_t total_ticks_ = 0;
};

} // namespace audio
