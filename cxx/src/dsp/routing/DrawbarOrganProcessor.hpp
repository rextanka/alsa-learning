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

    explicit DrawbarOrganProcessor(int sample_rate);

    void set_frequency(double freq) override {
        base_freq_ = freq;
        for (size_t i = 0; i < NUM_DRAWBARS; ++i)
            oscs_[i]->set_frequency(freq * kHarmonicRatios[i]);
    }

    void set_drawbar(size_t index, float value) {
        if (index < NUM_DRAWBARS)
            drawbar_gains_[index].set_target(value, ramp_samples_);
    }

    void reset() override { for (auto& osc : oscs_) osc->reset(); }

    PortType output_port_type() const override { return PortType::PORT_AUDIO; }

    bool apply_parameter(const std::string& name, float value) override;

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* context = nullptr) override;

private:
    [[maybe_unused]] int sample_rate_;
    int ramp_samples_;
    double base_freq_;

    std::array<std::unique_ptr<SineOscillatorProcessor>, NUM_DRAWBARS> oscs_;
    std::array<SmoothedParam, NUM_DRAWBARS> drawbar_gains_;
};

} // namespace audio

#endif // DRAWBAR_ORGAN_PROCESSOR_HPP
