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
#include "../DelayLine.hpp"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio {

class EchoDelayProcessor : public Processor {
public:
    explicit EchoDelayProcessor(int sample_rate)
        : sample_rate_(sample_rate)
        , delay_(sample_rate, 5.0f) // 5-second maximum
        , lfo_phase_(0.0)
    {
        delay_.set_delay_time(delay_time_);
        delay_.set_feedback(feedback_);
        delay_.set_mix(mix_);

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
            delay_time_ = std::clamp(value, 0.001f, 5.0f);
            delay_.set_delay_time(delay_time_);
            return true;
        }
        if (name == "feedback") {
            feedback_ = std::clamp(value, 0.0f, 0.95f);
            delay_.set_feedback(feedback_);
            return true;
        }
        if (name == "mix") {
            mix_ = std::clamp(value, 0.0f, 1.0f);
            delay_.set_mix(mix_);
            return true;
        }
        if (name == "mod_rate") {
            mod_rate_ = std::max(0.0f, value);
            return true;
        }
        if (name == "mod_intensity") {
            mod_intensity_ = std::clamp(value, 0.0f, 1.0f);
            return true;
        }
        return false;
    }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
        const double phase_inc = (sample_rate_ > 0 && mod_rate_ > 0.0f)
            ? static_cast<double>(mod_rate_) / sample_rate_
            : 0.0;

        for (auto& sample : output) {
            // Modulate delay time with LFO
            if (mod_rate_ > 0.0f && mod_intensity_ > 0.0f) {
                float lfo = static_cast<float>(std::sin(2.0 * M_PI * lfo_phase_));
                float mod = delay_time_ * mod_intensity_ * lfo;
                delay_.set_delay_time(std::max(0.001f, delay_time_ + mod));
                lfo_phase_ += phase_inc;
                if (lfo_phase_ >= 1.0) lfo_phase_ -= 1.0;
            }
            sample = delay_.process_sample(sample);
        }

        // Restore nominal delay time after block (so feedback path is consistent)
        if (mod_rate_ > 0.0f && mod_intensity_ > 0.0f) {
            delay_.set_delay_time(delay_time_);
        }
    }

private:
    int sample_rate_;
    DelayLine delay_;

    float  delay_time_    = 0.25f;
    float  feedback_      = 0.3f;
    float  mix_           = 0.5f;
    float  mod_rate_      = 0.0f;
    float  mod_intensity_ = 0.0f;
    double lfo_phase_;
};

} // namespace audio

#endif // ECHO_DELAY_PROCESSOR_HPP
