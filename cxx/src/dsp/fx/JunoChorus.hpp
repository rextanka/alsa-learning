/**
 * @file JunoChorus.hpp
 * @brief Dual-rate BBD delay emulation for the classic Juno stereo width.
 */

#ifndef JUNO_CHORUS_HPP
#define JUNO_CHORUS_HPP

#include "Processor.hpp"
#include "DelayLine.hpp"
#include <cmath>
#include <vector>

namespace audio {

/**
 * @brief Emulates the Juno-60 stereo BBD chorus.
 * 
 * Hardware Specs:
 * Mode I: ~0.4 Hz LFO
 * Mode II: ~0.6 Hz LFO
 * Both: ~1.0 Hz LFO
 * Delay time: ~1.5ms to 5ms range.
 */
class JunoChorus : public Processor {
public:
    enum class Mode {
        Off,
        I,
        II,
        I_II
    };

    explicit JunoChorus(int sample_rate)
        : sample_rate_(sample_rate)
        , delay_l_(sample_rate, 0.01) // 10ms max delay
        , delay_r_(sample_rate, 0.01)
    {
        delay_l_.set_feedback(0.0f);
        delay_r_.set_feedback(0.0f);
        delay_l_.set_mix(0.5f);
        delay_r_.set_mix(0.5f);
    }

    void set_mode(Mode mode) {
        mode_ = mode;
        switch (mode) {
            case Mode::I:    lfo_rate_ = 0.4; lfo_depth_ = 0.002; break; // 2ms depth
            case Mode::II:   lfo_rate_ = 0.6; lfo_depth_ = 0.002; break;
            case Mode::I_II: lfo_rate_ = 1.0; lfo_depth_ = 0.003; break;
            default:         lfo_rate_ = 0.0; lfo_depth_ = 0.0; break;
        }
    }

    void reset() override {
        delay_l_.reset();
        delay_r_.reset();
        lfo_phase_ = 0.0;
    }

protected:
    void do_pull(AudioBuffer& output, const VoiceContext* /* context */ = nullptr) override {
        if (mode_ == Mode::Off) return;

        const size_t frames = output.frames();
        const double phase_inc = lfo_rate_ / sample_rate_;

        for (size_t i = 0; i < frames; ++i) {
            // LFO is a sine
            double mod = std::sin(2.0 * M_PI * lfo_phase_);
            lfo_phase_ += phase_inc;
            if (lfo_phase_ >= 1.0) lfo_phase_ -= 1.0;

            // Stereo width is achieved by inverting the LFO for one channel
            float delay_ms_l = static_cast<float>(0.0035 + mod * lfo_depth_); // 3.5ms base
            float delay_ms_r = static_cast<float>(0.0035 - mod * lfo_depth_);

            delay_l_.set_delay_time(delay_ms_l);
            delay_r_.set_delay_time(delay_ms_r);

            float left = output.left[i];
            float right = output.right[i];

            output.left[i] = delay_l_.process_sample(left);
            output.right[i] = delay_r_.process_sample(right);
        }
    }

    // Mono fallback
    void do_pull(std::span<float> output, const VoiceContext* /* context */ = nullptr) override {
        // Juno Chorus is inherently stereo, but we can do a mono mixdown if needed
        AudioBuffer temp;
        std::vector<float> l(output.size()), r(output.size());
        temp.left = l;
        temp.right = r;
        std::copy(output.begin(), output.end(), temp.left.begin());
        std::copy(output.begin(), output.end(), temp.right.begin());

        do_pull(temp);

        for (size_t i = 0; i < output.size(); ++i) {
            output[i] = (temp.left[i] + temp.right[i]) * 0.5f;
        }
    }

private:
    int sample_rate_;
    DelayLine delay_l_;
    DelayLine delay_r_;
    Mode mode_ = Mode::Off;
    double lfo_rate_ = 0.4;
    double lfo_depth_ = 0.002;
    double lfo_phase_ = 0.0;
};

} // namespace audio

#endif // JUNO_CHORUS_HPP
