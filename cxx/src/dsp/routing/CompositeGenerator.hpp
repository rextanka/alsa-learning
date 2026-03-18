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
        declare_port({"fm_in",     PORT_AUDIO,   PortDirection::IN});         // audio-rate FM input

        // Parameters
        declare_parameter({"saw_gain",       "Sawtooth Level",  0.0f, 1.0f, 0.0f});
        declare_parameter({"pulse_gain",     "Pulse Level",     0.0f, 1.0f, 0.0f});
        declare_parameter({"sine_gain",      "Sine Level",      0.0f, 1.0f, 1.0f});
        declare_parameter({"triangle_gain",  "Triangle Level",  0.0f, 1.0f, 0.0f});
        declare_parameter({"sub_gain",       "Sub Level",       0.0f, 1.0f, 0.0f});
        declare_parameter({"wavetable_gain", "Wavetable Level", 0.0f, 1.0f, 0.0f});
        declare_parameter({"noise_gain",     "Noise Level",     0.0f, 1.0f, 0.0f});
        declare_parameter({"pulse_width",    "Pulse Width",      0.0f,  0.5f,   0.5f});
        declare_parameter({"wavetable_type", "Wavetable Type",   0.0f,  8.0f,   0.0f});
        declare_parameter({"transpose",      "Transpose",      -24.0f, 24.0f,   0.0f});
        declare_parameter({"detune",         "Detune (cents)", -100.f,100.0f,   0.0f});
        declare_parameter({"fm_depth",       "FM Depth",        0.0f,  1.0f,   0.0f});
    }

    // --- Frequency ---

    void set_frequency(double freq) override {
        base_freq_ = freq;
        double adjusted = freq
            * std::pow(2.0, transpose_ / 12.0)
            * std::pow(2.0, static_cast<double>(detune_cents_) / 1200.0);
        pulse_osc_->set_frequency(adjusted);
        saw_osc_->set_frequency(adjusted);
        sine_osc_->set_frequency(adjusted);
        tri_osc_->set_frequency(adjusted);
        wavetable_osc_->setFrequency(adjusted);
        // SubOscillator tracks the pulse oscillator's phase — no independent freq.
        // WhiteNoiseProcessor is aperiodic — no frequency to set.
    }

    // --- Named parameter dispatch ---

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "saw_gain")       { mixer_->set_gain(0, value); return true; }
        if (name == "pulse_gain")     { mixer_->set_gain(1, value); return true; }
        if (name == "sub_gain")       { mixer_->set_gain(2, value); return true; }
        if (name == "sine_gain")      { mixer_->set_gain(3, value); return true; }
        if (name == "triangle_gain")  { mixer_->set_gain(4, value); return true; }
        if (name == "wavetable_gain") { mixer_->set_gain(5, value); return true; }
        if (name == "noise_gain")     { mixer_->set_gain(6, value); return true; }
        if (name == "pulse_width" || name == "osc_pw") {
            pulse_osc_->set_pulse_width(value); return true;
        }
        if (name == "wavetable_type") {
            wavetable_osc_->setWaveType(static_cast<WaveType>(static_cast<int>(value)));
            return true;
        }
        if (name == "transpose") {
            transpose_ = static_cast<double>(std::round(value));
            set_frequency(base_freq_); // reapply with new offset
            return true;
        }
        if (name == "detune") {
            detune_cents_ = value;
            set_frequency(base_freq_); // reapply with new offset
            return true;
        }
        if (name == "fm_depth") {
            fm_depth_ = value; return true;
        }
        if (name == "osc_frequency") {
            set_frequency(static_cast<double>(value)); return true;
        }
        return false;
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

    /**
     * @brief Inject the fm_in audio buffer for audio-rate frequency modulation.
     *
     * Called by Voice::pull_mono before do_pull() when VCO2.audio_out is
     * connected to VCO1.fm_in. The span is valid for the current block only.
     */
    void inject_audio(std::string_view port_name,
                      std::span<const float> audio) override {
        if (port_name == "fm_in") fm_in_ = audio;
    }

protected:
    /**
     * @brief RT-SAFE: per-sample tick loop.
     *
     * Fills output with the soft-saturated mix of all oscillators weighted
     * by the SourceMixer gains. When fm_in_ is non-empty, each oscillator's
     * frequency is shifted by fm_depth_ * fm_in[i] octaves before the sample
     * is generated, then restored — giving audio-rate VCO-to-VCO FM.
     */
    void do_pull(std::span<float> output,
                 const VoiceContext* context = nullptr) override {
        auto* pulse = pulse_osc_.get();
        auto* sub   = sub_osc_.get();
        const bool has_fm = !fm_in_.empty() && fm_depth_ > 0.0f;

        for (size_t i = 0; i < output.size(); ++i) {
            if (has_fm) {
                // Apply per-sample FM: shift frequency by fm_depth * fm_in[i] octaves.
                const double fm_oct = static_cast<double>(fm_depth_ * fm_in_[i]);
                const double f_mod  = base_freq_
                    * std::pow(2.0, transpose_ / 12.0)
                    * std::pow(2.0, static_cast<double>(detune_cents_) / 1200.0)
                    * std::pow(2.0, fm_oct);
                pulse_osc_->set_frequency(f_mod);
                saw_osc_->set_frequency(f_mod);
                sine_osc_->set_frequency(f_mod);
                tri_osc_->set_frequency(f_mod);
                wavetable_osc_->setFrequency(f_mod);
            }

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
        fm_in_ = {}; // clear after use — injected spans are per-block only
    }

private:
    [[maybe_unused]] int sample_rate_; // reserved for future per-sample-rate reconfiguration
    double base_freq_    = 440.0; // last frequency set via set_frequency() before offsets
    double transpose_    = 0.0;   // semitones (−24–+24)
    float  detune_cents_ = 0.0f;  // cents (−100–+100)
    float  fm_depth_     = 0.0f;  // 0.0–1.0, fm_in scaling
    std::span<const float> fm_in_; // injected audio-rate FM signal (per-block, cleared after use)

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
