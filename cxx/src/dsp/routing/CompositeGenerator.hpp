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
    explicit CompositeGenerator(int sample_rate)
        : sample_rate_(sample_rate)
    {
        pulse_osc_      = std::make_unique<PulseOscillatorProcessor>(sample_rate);
        sub_osc_        = std::make_unique<SubOscillator>();
        saw_osc_        = std::make_unique<SawtoothOscillatorProcessor>(sample_rate);
        sine_osc_       = std::make_unique<SineOscillatorProcessor>(sample_rate);
        tri_osc_        = std::make_unique<TriangleOscillatorProcessor>(sample_rate);
        wavetable_osc_  = std::make_unique<WavetableOscillatorProcessor>(
                              static_cast<double>(sample_rate));
        noise_osc_      = std::make_unique<WhiteNoiseProcessor>();
        mixer_          = std::make_unique<SourceMixer>();

        set_tag("VCO");

        // Phase 15: named port declarations
        declare_port({"audio_out", PORT_AUDIO,   PortDirection::OUT});
        declare_port({"pitch_cv",  PORT_CONTROL, PortDirection::IN,  false}); // bipolar 1V/oct
        declare_port({"pwm_cv",    PORT_CONTROL, PortDirection::IN,  false}); // bipolar

        // Parameters
        declare_parameter({"saw_gain",       "Sawtooth Level",  0.0f, 1.0f, 0.0f});
        declare_parameter({"pulse_gain",     "Pulse Level",     0.0f, 1.0f, 0.0f});
        declare_parameter({"sine_gain",      "Sine Level",      0.0f, 1.0f, 1.0f});
        declare_parameter({"triangle_gain",  "Triangle Level",  0.0f, 1.0f, 0.0f});
        declare_parameter({"sub_gain",       "Sub Level",       0.0f, 1.0f, 0.0f});
        declare_parameter({"wavetable_gain", "Wavetable Level", 0.0f, 1.0f, 0.0f});
        declare_parameter({"noise_gain",     "Noise Level",     0.0f, 1.0f, 0.0f});
        declare_parameter({"pulse_width",    "Pulse Width",     0.0f, 0.5f, 0.5f});
        declare_parameter({"wavetable_type", "Wavetable Type",  0.0f, 8.0f, 0.0f});
    }

    // --- Frequency ---

    void set_frequency(double freq) override {
        pulse_osc_->set_frequency(freq);
        saw_osc_->set_frequency(freq);
        sine_osc_->set_frequency(freq);
        tri_osc_->set_frequency(freq);
        wavetable_osc_->setFrequency(freq);
        // SubOscillator tracks the pulse oscillator's phase — no independent freq.
        // WhiteNoiseProcessor is aperiodic — no frequency to set.
    }

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

    void reset() override {
        pulse_osc_->reset();
        sub_osc_->reset();
        saw_osc_->reset();
        sine_osc_->reset();
        tri_osc_->reset();
        wavetable_osc_->reset();
        noise_osc_->reset();
    }

    PortType output_port_type() const override { return PortType::PORT_AUDIO; }

protected:
    /**
     * @brief RT-SAFE: per-sample tick loop.
     *
     * Fills output with the soft-saturated mix of all oscillators weighted
     * by the SourceMixer gains. No allocations.
     */
    void do_pull(std::span<float> output,
                 const VoiceContext* context = nullptr) override {
        auto* pulse = pulse_osc_.get();
        auto* sub   = sub_osc_.get();

        for (size_t i = 0; i < output.size(); ++i) {
            float p_sample    = static_cast<float>(pulse->tick());
            float s_sample    = static_cast<float>(sub->generate_sample(pulse->get_phase()));
            float sine_sample = static_cast<float>(sine_osc_->tick());
            float tri_sample  = static_cast<float>(tri_osc_->tick());

            // Wavetable: pull one sample at a time via a one-element span
            float w_buf[1];
            std::span<float> w_span(w_buf, 1);
            wavetable_osc_->pull(w_span, context);

            std::array<float, SourceMixer::NUM_CHANNELS> inputs;
            inputs.fill(0.0f);
            inputs[0] = static_cast<float>(saw_osc_->tick());
            inputs[1] = p_sample;
            inputs[2] = s_sample;
            inputs[3] = sine_sample;
            inputs[4] = tri_sample;
            inputs[5] = w_buf[0];
            inputs[6] = noise_osc_->tick();

            output[i] = mixer_->mix(inputs);
        }
    }

private:
    [[maybe_unused]] int sample_rate_; // reserved for future per-sample-rate reconfiguration

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
