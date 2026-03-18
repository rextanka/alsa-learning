/**
 * @file SampleHoldProcessor.hpp
 * @brief Sample & Hold — freezes an input CV on each rising clock edge.
 *
 * Type name: SAMPLE_HOLD
 *
 * On each rising edge of clock_in (transition from ≤0.5 to >0.5), the current
 * value of cv_in is sampled and held until the next rising edge. The output is
 * the held value. Useful for stepped random modulation and arpeggio patterns.
 *
 * Ports (PORT_CONTROL):
 *   cv_in    IN  bipolar [-1,1]  — signal to sample (e.g. WHITE_NOISE, LFO saw)
 *   clock_in IN  unipolar [0,1]  — clock/trigger (e.g. LFO square at clock rate)
 *   cv_out   OUT bipolar [-1,1]
 *
 * Both inputs are populated via inject_cv() before do_pull().
 *
 * Usage — random stepped pitch:
 *   WHITE_NOISE:audio_out → SAMPLE_HOLD:cv_in   (needs audio→control cast; planned)
 *   LFO(square):control_out → SAMPLE_HOLD:clock_in
 *   SAMPLE_HOLD:cv_out → VCO:pitch_cv
 *
 * Usage — random filter color sweep:
 *   LFO(saw):control_out → SAMPLE_HOLD:cv_in
 *   LFO2(square):control_out → SAMPLE_HOLD:clock_in
 *   SAMPLE_HOLD:cv_out → VCF:cutoff_cv
 */

#ifndef SAMPLE_HOLD_PROCESSOR_HPP
#define SAMPLE_HOLD_PROCESSOR_HPP

#include "../Processor.hpp"
#include <algorithm>

namespace audio {

class SampleHoldProcessor : public Processor {
public:
    SampleHoldProcessor() {
        declare_port({"cv_in",    PORT_CONTROL, PortDirection::IN,  false});
        declare_port({"clock_in", PORT_CONTROL, PortDirection::IN,  true});  // unipolar
        declare_port({"cv_out",   PORT_CONTROL, PortDirection::OUT, false});
    }

    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

    void reset() override {
        held_value_   = 0.0f;
        prev_clock_   = false;
        cv_injected_  = {};
        clk_injected_ = {};
    }

    void inject_cv(std::string_view port_name, std::span<const float> cv) override {
        if      (port_name == "cv_in")    cv_injected_  = cv;
        else if (port_name == "clock_in") clk_injected_ = cv;
    }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
        for (size_t i = 0; i < output.size(); ++i) {
            float clk = clk_injected_.empty() ? 0.0f
                      : (i < clk_injected_.size() ? clk_injected_[i] : clk_injected_.back());
            float src = cv_injected_.empty()  ? 0.0f
                      : (i < cv_injected_.size()  ? cv_injected_[i]  : cv_injected_.back());

            const bool high = (clk > 0.5f);
            if (high && !prev_clock_) held_value_ = src; // rising edge → sample
            prev_clock_ = high;
            output[i]   = held_value_;
        }
        cv_injected_  = {};
        clk_injected_ = {};
    }

private:
    float held_value_   = 0.0f;
    bool  prev_clock_   = false;
    std::span<const float> cv_injected_;
    std::span<const float> clk_injected_;
};

} // namespace audio

#endif // SAMPLE_HOLD_PROCESSOR_HPP
