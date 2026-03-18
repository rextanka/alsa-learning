/**
 * @file InverterProcessor.hpp
 * @brief CV signal inverter/scaler — multiplies a PORT_CONTROL input by a scale factor.
 *
 * Usage: connect ADSR:envelope_out → INV:cv_in, then INV:cv_out → VCF:cutoff_cv
 * with scale=-1.0 to invert the envelope for a closing-filter pluck (harpsichord).
 *
 * PORT_CONTROL in:  cv_in  (bipolar [-1,1] or unipolar [0,1] depending on source)
 * PORT_CONTROL out: cv_out (same convention as input, scaled by `scale`)
 *
 * Note: This processor is a mod_source (output_port_type = PORT_CONTROL) and is
 * placed in Voice::mod_sources_. cv_in is populated via inject_cv() by the Phase 17
 * inter-mod routing in Voice::pull_mono — the source mod_source must precede this
 * node in mod_sources_ (add it first in the patch).
 */

#ifndef INVERTER_PROCESSOR_HPP
#define INVERTER_PROCESSOR_HPP

#include "../Processor.hpp"
#include <algorithm>
#include <cmath>

namespace audio {

class InverterProcessor : public Processor {
public:
    InverterProcessor() {
        declare_port({"cv_in",  PORT_CONTROL, PortDirection::IN,  false}); // bipolar
        declare_port({"cv_out", PORT_CONTROL, PortDirection::OUT, false}); // bipolar

        declare_parameter({"scale", "Scale", -2.0f, 2.0f, -1.0f});
    }

    void reset() override { injected_ = {}; }

    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "scale") { scale_ = value; return true; }
        return false;
    }

    void inject_cv(std::string_view port_name, std::span<const float> cv) override {
        if (port_name == "cv_in") injected_ = cv;
    }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
        if (!injected_.empty()) {
            size_t n = std::min(output.size(), injected_.size());
            for (size_t i = 0; i < n; ++i) output[i] = scale_ * injected_[i];
            for (size_t i = n; i < output.size(); ++i) output[i] = 0.0f;
        } else {
            // No input connected — output zero (not scale_, which would be a DC bias)
            for (auto& s : output) s = 0.0f;
        }
        injected_ = {}; // clear after use
    }

private:
    float scale_ = -1.0f;
    std::span<const float> injected_;
};

} // namespace audio

#endif // INVERTER_PROCESSOR_HPP
