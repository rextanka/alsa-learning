/**
 * @file SubOscillator.hpp
 * @brief Phase-locked square wave generator (f/2 or f/4).
 */

#ifndef SUB_OSCILLATOR_HPP
#define SUB_OSCILLATOR_HPP

#include "Processor.hpp"
#include <algorithm>

namespace audio {

/**
 * @brief Phase-locked square wave generator.
 * 
 * Instead of having its own phase accumulator, it tracks a parent's phase.
 * This ensures zero-drift alignment, essential for classic Roland emulations.
 */
class SubOscillator : public Processor {
public:
    enum class Octave {
        OneDown = 1, // f/2
        TwoDown = 2  // f/4
    };

    explicit SubOscillator(Octave octave = Octave::OneDown)
        : octave_(octave)
    {
    }

    void set_octave(Octave octave) {
        octave_ = octave;
    }

    /**
     * @brief Process and generate sub-oscillator sample based on parent phase.
     * 
     * @param parent_phase Current phase of the parent oscillator [0.0, 1.0)
     * @return double Sample value [-0.5, 0.5] (matches SquareOscillator scale)
     */
    double generate_sample(double parent_phase) {
        // Alignment Nuance: Flip phase to balance headroom
        // If parent phase < 0.5, sub is negative; if > 0.5, sub is positive.
        // For f/2 (OneDown), we flip state every time parent wraps.
        
        if (parent_phase < last_parent_phase_) {
            // Parent wrapped
            wrap_counter_++;
        }
        last_parent_phase_ = parent_phase;

        bool is_positive = false;
        if (octave_ == Octave::OneDown) {
            // flips every wrap: wrap_counter % 2
            // Phase alignment: offset by 0.5 of the parent to "push" when parent "pulls"
            is_positive = (wrap_counter_ % 2 == 0);
            if (parent_phase >= 0.5) is_positive = !is_positive;
        } else {
            // flips every 2 wraps: wrap_counter % 4
            is_positive = (wrap_counter_ % 4 < 2);
            if (parent_phase >= 0.5) is_positive = !is_positive;
        }

        return is_positive ? 0.5 : -0.5;
    }

    void reset() override {
        last_parent_phase_ = 0.0;
        wrap_counter_ = 0;
    }

protected:
    void do_pull(std::span<float> output, const VoiceContext* /* context */ = nullptr) override {
        std::fill(output.begin(), output.end(), 0.0f);
    }

    void do_pull(AudioBuffer& output, const VoiceContext* /* context */ = nullptr) override {
        output.clear();
    }

private:
    Octave octave_;
    double last_parent_phase_ = 0.0;
    uint32_t wrap_counter_ = 0;
};

} // namespace audio

#endif // SUB_OSCILLATOR_HPP
