/**
 * @file CompositeGenerator.cpp
 * @brief CompositeGenerator do_pull, frequency/parameter dispatch, and self-registration.
 */
#include "CompositeGenerator.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <cmath>

namespace audio {

CompositeGenerator::CompositeGenerator(int sample_rate)
    : sample_rate_(sample_rate)
    , ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
{
    pulse_osc_     = std::make_unique<PulseOscillatorProcessor>(sample_rate);
    sub_osc_       = std::make_unique<SubOscillator>();
    saw_osc_       = std::make_unique<SawtoothOscillatorProcessor>(sample_rate);
    sine_osc_      = std::make_unique<SineOscillatorProcessor>(sample_rate);
    tri_osc_       = std::make_unique<TriangleOscillatorProcessor>(sample_rate);
    wavetable_osc_ = std::make_unique<WavetableOscillatorProcessor>(
                         static_cast<double>(sample_rate));
    noise_osc_     = std::make_unique<WhiteNoiseProcessor>();
    mixer_         = std::make_unique<SourceMixer>();

    set_tag("VCO");

    declare_port({"audio_out", PORT_AUDIO,   PortDirection::OUT});
    declare_port({"sync_out",  PORT_AUDIO,   PortDirection::OUT});
    declare_port({"pitch_base_cv", PORT_CONTROL, PortDirection::IN, false,
                  "Absolute 1 V/oct pitch from MIDI_CV (C4 = 0 V). Sums with pitch_cv "
                  "modulation offset. Hardware: M-110 KBD CV input."});
    declare_port({"pitch_cv",  PORT_CONTROL, PortDirection::IN,  false,
                  "Modulation pitch offset in V/oct (LFO vibrato, portamento, bend). "
                  "Adds to pitch_base_cv (or to base_frequency_ when MIDI_CV is absent)."});
    declare_port({"pwm_cv",    PORT_CONTROL, PortDirection::IN,  false});
    declare_port({"fm_in",     PORT_AUDIO,   PortDirection::IN});
    declare_port({"sync_in",   PORT_AUDIO,   PortDirection::IN});

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
    declare_parameter({"footage",        "Footage (2/4/8/16/32)", 2.0f, 32.0f, 8.0f});
    declare_parameter({"detune",         "Detune (cents)", -100.f, 100.0f,  0.0f});
    declare_parameter({"fm_depth",       "FM Depth",        0.0f,  1.0f,   0.0f});
}

void CompositeGenerator::set_frequency(double freq) {
    base_freq_ = freq;
    double adjusted = freq
        * std::pow(2.0, transpose_ / 12.0)
        * std::pow(2.0, static_cast<double>(detune_.get()) / 1200.0);
    pulse_osc_->set_frequency(adjusted);
    saw_osc_->set_frequency(adjusted);
    sine_osc_->set_frequency(adjusted);
    tri_osc_->set_frequency(adjusted);
    wavetable_osc_->setFrequency(adjusted);
}

bool CompositeGenerator::apply_parameter(const std::string& name, float value) {
    if (name == "saw_gain")       { saw_gain_.set_target(value, ramp_samples_);       gain_set_[0] = true; return true; }
    if (name == "pulse_gain")     { pulse_gain_.set_target(value, ramp_samples_);     gain_set_[1] = true; return true; }
    if (name == "sub_gain")       { sub_gain_.set_target(value, ramp_samples_);       gain_set_[2] = true; return true; }
    if (name == "sine_gain")      { sine_gain_.set_target(value, ramp_samples_);      gain_set_[3] = true; return true; }
    if (name == "triangle_gain")  { tri_gain_.set_target(value, ramp_samples_);       gain_set_[4] = true; return true; }
    if (name == "wavetable_gain") { wavetable_gain_.set_target(value, ramp_samples_); gain_set_[5] = true; return true; }
    if (name == "noise_gain")     { noise_gain_.set_target(value, ramp_samples_);     gain_set_[6] = true; return true; }
    if (name == "pulse_width" || name == "osc_pw") {
        pulse_width_.set_target(value, ramp_samples_); return true;
    }
    if (name == "wavetable_type") {
        wavetable_osc_->setWaveType(static_cast<WaveType>(static_cast<int>(value)));
        return true;
    }
    if (name == "transpose") {
        transpose_ = static_cast<double>(std::round(value));
        set_frequency(base_freq_);
        return true;
    }
    if (name == "detune") {
        detune_.set_target(value, ramp_samples_);
        set_frequency(base_freq_);
        return true;
    }
    if (name == "fm_depth") {
        fm_depth_.set_target(value, ramp_samples_); return true;
    }
    if (name == "osc_frequency") {
        set_frequency(static_cast<double>(value)); return true;
    }
    // footage: Roland-style range selector (2/4/8/16/32 foot) → semitone transpose.
    // 32' = -24 st, 16' = -12 st, 8' = 0 st (concert pitch), 4' = +12 st, 2' = +24 st.
    if (name == "footage") {
        const int ft = static_cast<int>(std::round(value));
        int semitones = 0;
        switch (ft) {
            case  2: semitones = +24; break;
            case  4: semitones = +12; break;
            case  8: semitones =   0; break;
            case 16: semitones = -12; break;
            case 32: semitones = -24; break;
            default: break;  // unknown footage — no-op
        }
        transpose_ = static_cast<double>(semitones);
        set_frequency(base_freq_);
        return true;
    }
    return false;
}

void CompositeGenerator::reset() {
    pulse_osc_->reset();
    sub_osc_->reset();
    saw_osc_->reset();
    sine_osc_->reset();
    tri_osc_->reset();
    wavetable_osc_->reset();
    noise_osc_->reset();
    // Snap all gain SmoothedParams to their targets so the first note
    // starts with the correct timbre immediately rather than ramping
    // from 0 — prevents a timbral "click" onset that masks the ADSR attack.
    saw_gain_.snap();
    pulse_gain_.snap();
    sub_gain_.snap();
    sine_gain_.snap();
    tri_gain_.snap();
    wavetable_gain_.snap();
    noise_gain_.snap();
}

void CompositeGenerator::do_pull(std::span<float> output, const VoiceContext* context) {
    const int n = static_cast<int>(output.size());

    saw_gain_.advance(n);
    pulse_gain_.advance(n);
    sub_gain_.advance(n);
    sine_gain_.advance(n);
    tri_gain_.advance(n);
    wavetable_gain_.advance(n);
    noise_gain_.advance(n);
    pulse_width_.advance(n);
    detune_.advance(n);
    fm_depth_.advance(n);

    if (gain_set_[0]) mixer_->set_gain(0, saw_gain_.get());
    if (gain_set_[1]) mixer_->set_gain(1, pulse_gain_.get());
    if (gain_set_[2]) mixer_->set_gain(2, sub_gain_.get());
    if (gain_set_[3]) mixer_->set_gain(3, sine_gain_.get());
    if (gain_set_[4]) mixer_->set_gain(4, tri_gain_.get());
    if (gain_set_[5]) mixer_->set_gain(5, wavetable_gain_.get());
    if (gain_set_[6]) mixer_->set_gain(6, noise_gain_.get());
    pulse_osc_->set_pulse_width(pulse_width_.get());

    auto* pulse = pulse_osc_.get();
    auto* sub   = sub_osc_.get();
    const float fm_depth_val = fm_depth_.get();
    const bool has_fm   = !fm_in_.empty() && fm_depth_val > 0.0f;
    const bool has_sync = !sync_in_.empty();

    // Prepare sync output buffer.
    sync_buf_size_ = std::min(static_cast<size_t>(n), kMaxBlockSize);
    std::fill(sync_buf_.begin(), sync_buf_.begin() + sync_buf_size_, 0.0f);

    for (size_t i = 0; i < n; ++i) {
        // Hard sync: reset all oscillator phases when master triggers.
        if (has_sync && sync_in_[i] > 0.5f) {
            pulse_osc_->reset_phase();
            saw_osc_->reset_phase();
            sine_osc_->reset_phase();
            tri_osc_->reset_phase();
            wavetable_osc_->reset();
            sub_osc_->reset();
        }

        if (has_fm) {
            const double fm_oct = static_cast<double>(fm_depth_val * fm_in_[i]);
            const double f_mod  = base_freq_
                * std::pow(2.0, transpose_ / 12.0)
                * std::pow(2.0, static_cast<double>(detune_.get()) / 1200.0)
                * std::pow(2.0, fm_oct);
            pulse_osc_->set_frequency(f_mod);
            saw_osc_->set_frequency(f_mod);
            sine_osc_->set_frequency(f_mod);
            tri_osc_->set_frequency(f_mod);
            wavetable_osc_->setFrequency(f_mod);
        }

        // Capture saw phase before tick to detect wrap (sync_out master logic).
        const double saw_phase_before = saw_osc_->get_phase();

        float p_sample    = static_cast<float>(pulse->tick());
        float s_sample    = static_cast<float>(sub->generate_sample(pulse->get_phase()));
        float sine_sample = static_cast<float>(sine_osc_->tick());
        float tri_sample  = static_cast<float>(tri_osc_->tick());

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

        // Emit sync pulse when saw phase wrapped this sample.
        if (i < kMaxBlockSize)
            sync_buf_[i] = (saw_osc_->get_phase() < saw_phase_before) ? 1.0f : 0.0f;
    }
    fm_in_   = {};
    sync_in_ = {};
}

namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "COMPOSITE_GENERATOR",
    "Multi-oscillator VCO: saw/pulse/sine/triangle/wavetable/noise/sub with FM input",
    "Default tag VCO. Set one or more *_gain params to activate oscillator layers. "
    "connect LFO:control_out → VCO:pitch_cv for vibrato. "
    "connect VCO2:audio_out → VCO1:fm_in with fm_depth > 0 for FM synthesis. "
    "transpose in semitones, detune in cents for detuned layers.",
    [](int sr) { return std::make_unique<CompositeGenerator>(sr); }
);
} // namespace

} // namespace audio
