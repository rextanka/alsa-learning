/**
 * @file JunoChorus.hpp 
 * @brief Dual-rate BBD delay emulation for the classic Juno stereo width.
 */

#ifndef JUNO_CHORUS_HPP
#define JUNO_CHORUS_HPP

#include "Processor.hpp"
#include "SmoothedParam.hpp"
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

    static constexpr float kRampSeconds = 0.010f; // 10 ms

    explicit JunoChorus(int sample_rate)
        : sample_rate_(sample_rate)
        , ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
        , delay_l_(sample_rate, 0.01) // 10ms max delay
        , delay_r_(sample_rate, 0.01)
    {
        delay_l_.set_feedback(0.0f);
        delay_r_.set_feedback(0.0f);
        delay_l_.set_mix(0.5f);
        delay_r_.set_mix(0.5f);

        // Phase 15: named port declarations
        declare_port({"audio_in",  PORT_AUDIO, PortDirection::IN});
        declare_port({"audio_out", PORT_AUDIO, PortDirection::OUT});

        declare_parameter({"mode",  "Chorus Mode",  0.0f,  3.0f, 0.0f});
        declare_parameter({"rate",  "Chorus Rate",  0.1f, 10.0f, 0.5f});
        declare_parameter({"depth", "Chorus Depth", 0.0f,  1.0f, 0.5f});
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "mode") {
            set_mode(static_cast<Mode>(static_cast<int>(std::round(value)))); // snap
            return true;
        }
        if (name == "rate") {
            lfo_rate_.set_target(static_cast<float>(value), ramp_samples_);
            return true;
        }
        if (name == "depth") {
            // depth [0,1] → modulation depth in seconds (max ≈ 3ms)
            lfo_depth_.set_target(static_cast<float>(value) * 0.003f, ramp_samples_);
            return true;
        }
        return false;
    }

    void set_mode(Mode mode) {
        mode_ = mode; // snap — no smoothing on mode switch
        switch (mode) {
            case Mode::I:    lfo_rate_.set_target(0.4f, 0); lfo_depth_.set_target(0.002f, 0); break;
            case Mode::II:   lfo_rate_.set_target(0.6f, 0); lfo_depth_.set_target(0.002f, 0); break;
            case Mode::I_II: lfo_rate_.set_target(1.0f, 0); lfo_depth_.set_target(0.003f, 0); break;
            default:         lfo_rate_.set_target(0.0f, 0); lfo_depth_.set_target(0.0f, 0); break;
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

        const int n = static_cast<int>(output.frames());
        lfo_rate_.advance(n);
        lfo_depth_.advance(n);

        const size_t frames = output.frames();
        const double phase_inc = static_cast<double>(lfo_rate_.get()) / static_cast<double>(sample_rate_);
        const double depth_val = static_cast<double>(lfo_depth_.get());

        for (size_t i = 0; i < frames; ++i) {
            // LFO is a sine
            double mod = std::sin(2.0 * M_PI * lfo_phase_);
            lfo_phase_ += phase_inc;
            if (lfo_phase_ >= 1.0) lfo_phase_ -= 1.0;

            // Stereo width is achieved by inverting the LFO for one channel
            float delay_ms_l = static_cast<float>(0.0035 + mod * depth_val); // 3.5ms base
            float delay_ms_r = static_cast<float>(0.0035 - mod * depth_val);

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
    int ramp_samples_;
    DelayLine delay_l_;
    DelayLine delay_r_;
    Mode mode_ = Mode::Off;
    SmoothedParam lfo_rate_{0.4f};
    SmoothedParam lfo_depth_{0.002f};
    double lfo_phase_ = 0.0;
};

} // namespace audio

#endif // JUNO_CHORUS_HPP
