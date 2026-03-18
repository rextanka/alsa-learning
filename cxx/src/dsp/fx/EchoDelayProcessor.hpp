/**
 * @file EchoDelayProcessor.hpp
 * @brief Modulated delay line (BBD-style) for echo and metallic shimmer effects.
 *
 * Wraps a DelayLine with:
 *   - LFO modulation of delay time (mod_rate, mod_intensity) for chorus shimmer
 *   - Static delay time, feedback, and wet/dry mix parameters
 *
 * This is the ECHO_DELAY module type registered in ModuleRegistry.
 *
 * PORT_AUDIO in:  audio_in
 * PORT_AUDIO out: audio_out
 *
 * Parameters:
 *   time           0.0–5.0 s      base delay time
 *   feedback       0.0–0.95       feedback fraction
 *   mix            0.0–1.0        wet/dry (0=dry, 1=wet)
 *   mod_rate       0.0–20.0 Hz    LFO rate for delay modulation (0=static)
 *   mod_intensity  0.0–1.0        LFO depth as fraction of delay time
 *
 * Feedback connections (cymbal patch) are parsed by engine_load_patch and stored,
 * but not executed until Phase 17 graph executor support lands.
 */

#ifndef ECHO_DELAY_PROCESSOR_HPP
#define ECHO_DELAY_PROCESSOR_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
#include "../DelayLine.hpp"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

class EchoDelayProcessor : public Processor {
public:
    static constexpr float kRampSeconds = 0.010f; // 10 ms

    explicit EchoDelayProcessor(int sample_rate)
        : sample_rate_(sample_rate)
        , ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
        , delay_(sample_rate, 5.0f) // 5-second maximum
        , lfo_phase_(0.0)
    {
        delay_.set_delay_time(delay_time_.get());
        delay_.set_feedback(feedback_.get());
        delay_.set_mix(mix_.get());

        declare_port({"audio_in",  PORT_AUDIO, PortDirection::IN});
        declare_port({"audio_out", PORT_AUDIO, PortDirection::OUT});

        declare_parameter({"time",          "Delay Time",      0.0f,  5.0f, 0.25f, true});
        declare_parameter({"feedback",      "Feedback",        0.0f,  0.95f, 0.3f});
        declare_parameter({"mix",           "Wet/Dry Mix",     0.0f,  1.0f,  0.5f});
        declare_parameter({"mod_rate",      "Mod Rate (Hz)",   0.0f, 20.0f,  0.0f});
        declare_parameter({"mod_intensity", "Mod Intensity",   0.0f,  1.0f,  0.0f});
    }

    void reset() override {
        delay_.reset();
        lfo_phase_ = 0.0;
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "time") {
            // Snap immediately — delay time is a patch-configuration value set before
            // audio starts. A gradual ramp would use the wrong initial delay time.
            delay_time_.set_target(std::clamp(value, 0.001f, 5.0f), 0);
            delay_.set_delay_time(delay_time_.get());
            return true;
        }
        if (name == "feedback") {
            feedback_.set_target(std::clamp(value, 0.0f, 0.95f), ramp_samples_);
            delay_.set_feedback(feedback_.get());
            return true;
        }
        if (name == "mix") {
            mix_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_);
            delay_.set_mix(mix_.get());
            return true;
        }
        if (name == "mod_rate") {
            mod_rate_.set_target(std::max(0.0f, value), ramp_samples_);
            return true;
        }
        if (name == "mod_intensity") {
            mod_intensity_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_);
            return true;
        }
        return false;
    }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
        const int n = static_cast<int>(output.size());
        delay_time_.advance(n);
        feedback_.advance(n);
        mix_.advance(n);
        mod_rate_.advance(n);
        mod_intensity_.advance(n);

        // Push smoothed feedback/mix to delay line if changed
        if (feedback_.is_ramping()) delay_.set_feedback(feedback_.get());
        if (mix_.is_ramping())      delay_.set_mix(mix_.get());

        const float dt_val  = delay_time_.get();
        const float mr_val  = mod_rate_.get();
        const float mi_val  = mod_intensity_.get();
        const double phase_inc = (sample_rate_ > 0 && mr_val > 0.0f)
            ? static_cast<double>(mr_val) / static_cast<double>(sample_rate_)
            : 0.0;

        for (auto& sample : output) {
            // Modulate delay time with LFO
            if (mr_val > 0.0f && mi_val > 0.0f) {
                float lfo = static_cast<float>(std::sin(2.0 * M_PI * lfo_phase_));
                float mod = dt_val * mi_val * lfo;
                delay_.set_delay_time(std::max(0.001f, dt_val + mod));
                lfo_phase_ += phase_inc;
                if (lfo_phase_ >= 1.0) lfo_phase_ -= 1.0;
            }
            sample = delay_.process_sample(sample);
        }

        // Restore nominal delay time after block (so feedback path is consistent)
        if (mr_val > 0.0f && mi_val > 0.0f) {
            delay_.set_delay_time(dt_val);
        }
    }

private:
    int sample_rate_;
    int ramp_samples_;
    DelayLine delay_;

    SmoothedParam delay_time_{0.25f};
    SmoothedParam feedback_{0.3f};
    SmoothedParam mix_{0.5f};
    SmoothedParam mod_rate_{0.0f};
    SmoothedParam mod_intensity_{0.0f};
    double lfo_phase_;
};

} // namespace audio

#endif // ECHO_DELAY_PROCESSOR_HPP
