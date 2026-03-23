/**
 * @file CompositeGenerator.hpp
 * @brief Composite oscillator node: owns all VCOs and SourceMixer, acts as the
 *        Generator (first node) in a Voice signal_chain_ (Phase 14).
 *
 * RT-SAFE: do_pull contains only the same per-sample tick loop that was
 * previously inlined in Voice::pull_mono. No allocations, no locks.
 *
 * SubOscillator is phase-coupled to the pulse oscillator — it cannot be an
 * independent chain node. CompositeGenerator keeps both together and drives
 * the SubOscillator from the pulse oscillator's phase on every sample.
 */

#ifndef COMPOSITE_GENERATOR_HPP
#define COMPOSITE_GENERATOR_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
#include "../oscillator/OscillatorProcessor.hpp"
#include "../oscillator/SawtoothOscillatorProcessor.hpp"
#include "../oscillator/SineOscillatorProcessor.hpp"
#include "../oscillator/TriangleOscillatorProcessor.hpp"
#include "../oscillator/WavetableOscillatorProcessor.hpp"
#include "../oscillator/SubOscillator.hpp"
#include "../oscillator/PulseOscillatorProcessor.hpp"
#include "../oscillator/WhiteNoiseProcessor.hpp"
#include "SourceMixer.hpp"
#include <memory>
#include <array>

namespace audio {

/**
 * @brief Composite generator node — wraps all oscillators and the SourceMixer.
 *
 * Default tag: "VCO". Placed as signal_chain_[0] in a baked Voice.
 *
 * Channel mapping (matches SourceMixer):
 *   0: Sawtooth
 *   1: Pulse
 *   2: Sub-Oscillator
 *   3: Sine
 *   4: Triangle
 *   5: Wavetable
 *   6: White Noise
 */
class CompositeGenerator : public Processor {
public:
    static constexpr float kRampSeconds = 0.010f; // 10 ms

    explicit CompositeGenerator(int sample_rate);

    // --- Frequency ---

    void set_frequency(double freq) override;

    // --- Named parameter dispatch ---

    bool apply_parameter(const std::string& name, float value) override;

    // --- Accessors for parameter routing ---

    PulseOscillatorProcessor& pulse_osc()         { return *pulse_osc_; }
    SawtoothOscillatorProcessor& saw_osc()        { return *saw_osc_; }
    SineOscillatorProcessor& sine_osc()           { return *sine_osc_; }
    TriangleOscillatorProcessor& tri_osc()        { return *tri_osc_; }
    WavetableOscillatorProcessor& wavetable_osc() { return *wavetable_osc_; }
    SubOscillator& sub_osc()                      { return *sub_osc_; }
    WhiteNoiseProcessor& noise_osc()              { return *noise_osc_; }
    SourceMixer& mixer()                          { return *mixer_; }

    // --- Processor interface ---

    void reset() override;

    PortType output_port_type() const override { return PortType::PORT_AUDIO; }

    /**
     * @brief Inject the fm_in audio buffer for audio-rate frequency modulation.
     *
     * Called by Voice::pull_mono before do_pull() when VCO2.audio_out is
     * connected to VCO1.fm_in. The span is valid for the current block only.
     */
    void inject_audio(std::string_view port_name,
                      std::span<const float> audio) override {
        if (port_name == "fm_in")   fm_in_   = audio;
        if (port_name == "sync_in") sync_in_ = audio;
    }

    /**
     * @brief Expose sync_out trigger buffer produced during the last do_pull().
     *
     * Returns a span of 1.0f pulses (one per saw-phase-wrap sample) for routing
     * to a slave COMPOSITE_GENERATOR's sync_in via Voice::pull_mono().
     */
    std::span<const float> get_secondary_output(std::string_view port_name) const override {
        if (port_name == "sync_out" && sync_buf_size_ > 0)
            return std::span<const float>(sync_buf_.data(), sync_buf_size_);
        return {};
    }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* context = nullptr) override;

private:
    // gain_set_[ch]: true once apply_parameter() has explicitly set that gain.
    bool gain_set_[7] = {};

    [[maybe_unused]] int sample_rate_;
    int ramp_samples_;

    double base_freq_    = 440.0;
    double transpose_    = 0.0;   // semitones from footage or transpose param (snap)
    double coarse_tune_  = 0.0;   // semitones added on top of transpose_ (snap)

    SmoothedParam saw_gain_{0.0f};
    SmoothedParam pulse_gain_{0.0f};
    SmoothedParam sub_gain_{0.0f};
    SmoothedParam sine_gain_{0.0f};
    SmoothedParam tri_gain_{0.0f};
    SmoothedParam wavetable_gain_{0.0f};
    SmoothedParam noise_gain_{0.0f};
    SmoothedParam pulse_width_{0.5f};
    SmoothedParam pw_env_depth_{0.0f};
    float         pw_env_cv_{0.0f};  // unipolar 0–1 from ADSR; reset each block
    SmoothedParam detune_{0.0f};
    SmoothedParam fm_depth_{0.0f};

    std::span<const float> fm_in_;
    std::span<const float> sync_in_;

    // Sync output: filled during do_pull() with 1.0f on saw-phase-wrap samples.
    static constexpr size_t kMaxBlockSize = 4096;
    std::array<float, kMaxBlockSize> sync_buf_{};
    size_t sync_buf_size_ = 0;

    std::unique_ptr<PulseOscillatorProcessor>    pulse_osc_;
    std::unique_ptr<SubOscillator>               sub_osc_;
    std::unique_ptr<SawtoothOscillatorProcessor> saw_osc_;
    std::unique_ptr<SineOscillatorProcessor>     sine_osc_;
    std::unique_ptr<TriangleOscillatorProcessor> tri_osc_;
    std::unique_ptr<WavetableOscillatorProcessor> wavetable_osc_;
    std::unique_ptr<WhiteNoiseProcessor>         noise_osc_;
    std::unique_ptr<SourceMixer>                 mixer_;
};

} // namespace audio

#endif // COMPOSITE_GENERATOR_HPP
