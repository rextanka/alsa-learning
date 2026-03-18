/**
 * @file DrawbarOrganProcessor.hpp
 * @brief Tonewheel-style drawbar organ: 9 sine partials at Hammond footage ratios.
 *
 * Harmonic ratios (footage → multiplier):
 *   16'   → 0.5   (sub-octave)
 *   5⅓'  → 1.5   (quint)
 *   8'    → 1.0   (fundamental, "principal")
 *   4'    → 2.0   (octave)
 *   2⅔'  → 3.0   (nazard)
 *   2'    → 4.0   (super-octave)
 *   1⅗'  → 5.0   (tierce)
 *   1⅓'  → 6.0   (larigot)
 *   1'    → 8.0   (sifflöte)
 *
 * Each drawbar is a float in [0, 8] — nine steps of 1/8 gain per step,
 * matching the original Hammond register-wheel click-stop convention.
 *
 * RT-SAFE: no heap allocation in do_pull. All oscillators are pre-allocated.
 */

#ifndef DRAWBAR_ORGAN_PROCESSOR_HPP
#define DRAWBAR_ORGAN_PROCESSOR_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
#include "../oscillator/SineOscillatorProcessor.hpp"
#include <array>
#include <memory>

namespace audio {

class DrawbarOrganProcessor : public Processor {
public:
    static constexpr size_t NUM_DRAWBARS = 9;

    // Hammond footage multipliers, indexed 0–8
    static constexpr std::array<double, NUM_DRAWBARS> kHarmonicRatios = {
        0.5,  // 16'  sub-octave
        1.5,  // 5⅓' quint
        1.0,  // 8'   principal (fundamental)
        2.0,  // 4'   octave
        3.0,  // 2⅔' nazard
        4.0,  // 2'   super-octave
        5.0,  // 1⅗' tierce
        6.0,  // 1⅓' larigot
        8.0,  // 1'   sifflöte
    };

    static constexpr float kRampSeconds = 0.010f; // 10 ms

    explicit DrawbarOrganProcessor(int sample_rate)
        : sample_rate_(sample_rate)
        , ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
        , base_freq_(0.0)
    {
        for (size_t i = 0; i < NUM_DRAWBARS; ++i) {
            oscs_[i] = std::make_unique<SineOscillatorProcessor>(sample_rate);
        }
        // Default: 8' drawbar fully open (classic "flute" preset)
        drawbar_gains_[2].set_target(8.0f, 0); // snap to initial value

        set_tag("ORGAN");

        declare_port({"audio_out", PORT_AUDIO,   PortDirection::OUT});
        declare_port({"pitch_cv",  PORT_CONTROL, PortDirection::IN, false});

        declare_parameter({"drawbar_16",  "16' Sub-Octave",  0.0f, 8.0f, 0.0f});
        declare_parameter({"drawbar_513", "5⅓' Quint",      0.0f, 8.0f, 0.0f});
        declare_parameter({"drawbar_8",   "8' Principal",    0.0f, 8.0f, 8.0f});
        declare_parameter({"drawbar_4",   "4' Octave",       0.0f, 8.0f, 0.0f});
        declare_parameter({"drawbar_223", "2⅔' Nazard",     0.0f, 8.0f, 0.0f});
        declare_parameter({"drawbar_2",   "2' Super-Octave", 0.0f, 8.0f, 0.0f});
        declare_parameter({"drawbar_135", "1⅗' Tierce",     0.0f, 8.0f, 0.0f});
        declare_parameter({"drawbar_113", "1⅓' Larigot",    0.0f, 8.0f, 0.0f});
        declare_parameter({"drawbar_1",   "1' Sifflöte",    0.0f, 8.0f, 0.0f});
    }

    void set_frequency(double freq) override {
        base_freq_ = freq;
        for (size_t i = 0; i < NUM_DRAWBARS; ++i) {
            oscs_[i]->set_frequency(freq * kHarmonicRatios[i]);
        }
    }

    void set_drawbar(size_t index, float value) {
        if (index < NUM_DRAWBARS) {
            drawbar_gains_[index].set_target(value, ramp_samples_);
        }
    }

    void reset() override {
        for (auto& osc : oscs_) osc->reset();
    }

    PortType output_port_type() const override { return PortType::PORT_AUDIO; }

    bool apply_parameter(const std::string& name, float value) override {
        // Map parameter names to drawbar indices
        static constexpr const char* kParamNames[NUM_DRAWBARS] = {
            "drawbar_16", "drawbar_513", "drawbar_8",   "drawbar_4",
            "drawbar_223", "drawbar_2",  "drawbar_135", "drawbar_113",
            "drawbar_1"
        };
        for (size_t i = 0; i < NUM_DRAWBARS; ++i) {
            if (name == kParamNames[i]) {
                drawbar_gains_[i].set_target(value, ramp_samples_);
                return true;
            }
        }
        return false;
    }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /*context*/ = nullptr) override {
        // Normalise: max combined output is 9 drawbars × 1.0 amplitude.
        // We scale by 1/9 so the output never exceeds unity.
        constexpr float kNorm = 1.0f / 9.0f;

        const int n_frames = static_cast<int>(output.size());
        for (size_t i = 0; i < NUM_DRAWBARS; ++i) {
            drawbar_gains_[i].advance(n_frames);
        }

        for (size_t n = 0; n < output.size(); ++n) {
            float sum = 0.0f;
            for (size_t i = 0; i < NUM_DRAWBARS; ++i) {
                const float gain = drawbar_gains_[i].get();
                if (gain > 0.0f) {
                    sum += static_cast<float>(oscs_[i]->tick()) * (gain / 8.0f);
                }
            }
            output[n] = sum * kNorm;
        }
    }

private:
    [[maybe_unused]] int sample_rate_;
    int ramp_samples_;
    double base_freq_;

    std::array<std::unique_ptr<SineOscillatorProcessor>, NUM_DRAWBARS> oscs_;
    std::array<SmoothedParam, NUM_DRAWBARS> drawbar_gains_;
};

} // namespace audio

#endif // DRAWBAR_ORGAN_PROCESSOR_HPP
