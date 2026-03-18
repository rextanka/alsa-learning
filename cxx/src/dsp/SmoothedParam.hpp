/**
 * @file SmoothedParam.hpp
 * @brief Zipper-free linear parameter interpolation for DSP processors.
 *
 * SmoothedParam replaces a raw float member variable for any parameter that
 * would produce an audible click if changed instantaneously. When set_target()
 * is called, the value ramps linearly from its current value to the target
 * over ramp_samples. Snap parameters set current = target immediately.
 *
 * Usage in a processor:
 *   SmoothedParam cutoff_{20000.f};          // smooth by default
 *   SmoothedParam wavetype_{0.f, true};      // snap (discrete selector)
 *
 *   // In apply_parameter:
 *   cutoff_.set_target(value, ramp_samples_);
 *
 *   // In do_pull:
 *   cutoff_.advance(static_cast<int>(frames));
 *   const float fc = cutoff_.get();
 */

#ifndef AUDIO_SMOOTHED_PARAM_HPP
#define AUDIO_SMOOTHED_PARAM_HPP

#include <algorithm>

namespace audio {

struct SmoothedParam {
    explicit SmoothedParam(float default_value = 0.f, bool snap = false)
        : current_(default_value)
        , target_(default_value)
        , step_(0.f)
        , remaining_(0)
        , snap_(snap)
    {}

    /**
     * Schedule a transition to @p target over @p ramp_samples samples.
     * If snap_ is true the value is applied immediately.
     */
    void set_target(float target, int ramp_samples) {
        if (snap_ || ramp_samples <= 0) {
            current_ = target_ = target;
            remaining_ = 0;
            step_      = 0.f;
            return;
        }
        if (target == target_ && remaining_ > 0) return; // already ramping there
        target_    = target;
        remaining_ = ramp_samples;
        step_      = (target_ - current_) / static_cast<float>(ramp_samples);
    }

    /**
     * Advance the ramp by @p n samples.
     * Call once per block at the start of do_pull().
     */
    void advance(int n) {
        if (remaining_ <= 0) return;
        const int steps = std::min(n, remaining_);
        current_   += step_ * static_cast<float>(steps);
        remaining_ -= steps;
        if (remaining_ <= 0) current_ = target_;
    }

    float get() const { return current_; }

    bool is_ramping() const { return remaining_ > 0; }

    /** Immediately snap current value to the target, cancelling any active ramp. */
    void snap() {
        current_   = target_;
        remaining_ = 0;
        step_      = 0.f;
    }

private:
    float current_;
    float target_;
    float step_;
    int   remaining_;
    bool  snap_;
};

} // namespace audio

#endif // AUDIO_SMOOTHED_PARAM_HPP
