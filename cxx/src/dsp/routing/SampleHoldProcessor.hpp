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
 */

#ifndef SAMPLE_HOLD_PROCESSOR_HPP
#define SAMPLE_HOLD_PROCESSOR_HPP

#include "../Processor.hpp"
#include <algorithm>

namespace audio {

class SampleHoldProcessor : public Processor {
public:
    SampleHoldProcessor();

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
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

private:
    float held_value_   = 0.0f;
    bool  prev_clock_   = false;
    std::span<const float> cv_injected_;
    std::span<const float> clk_injected_;
};

} // namespace audio

#endif // SAMPLE_HOLD_PROCESSOR_HPP
