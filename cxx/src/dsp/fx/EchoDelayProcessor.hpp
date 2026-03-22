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
#include "../TempoSync.hpp"

namespace audio {

class EchoDelayProcessor : public Processor {
public:
    static constexpr float kRampSeconds = 0.010f; // 10 ms

    explicit EchoDelayProcessor(int sample_rate);

    void reset() override {
        delay_.reset();
        lfo_phase_ = 0.0;
    }

    bool apply_parameter(const std::string& name, float value) override;

    void inject_cv(std::string_view port, std::span<const float> data) override {
        if (port == "time_cv") time_cv_in_ = data.empty() ? 0.0f : data[0];
    }

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

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
    float time_cv_in_ = 0.0f;  ///< Additive delay-time offset (seconds) injected each block.

    // Tempo-sync (Phase 27D) — off by default, fully backward-compatible.
    bool sync_     = false;
    int  division_ = 2; ///< Index into kDivisionMultipliers; 2 = "quarter"
};

} // namespace audio

#endif // ECHO_DELAY_PROCESSOR_HPP
