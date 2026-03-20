/**
 * @file JunoChorus.hpp
 * @brief Dual-rate BBD delay emulation for the classic Juno stereo width.
 */

#ifndef JUNO_CHORUS_HPP
#define JUNO_CHORUS_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
#include "../DelayLine.hpp"
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

    explicit JunoChorus(int sample_rate);

    bool apply_parameter(const std::string& name, float value) override;

    void set_mode(Mode mode);

    void reset() override {
        delay_l_.reset();
        delay_r_.reset();
        lfo_phase_ = 0.0;
    }

protected:
    void do_pull(AudioBuffer& output, const VoiceContext* context = nullptr) override;
    void do_pull(std::span<float> output, const VoiceContext* context = nullptr) override;

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
