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
 * placed in Voice::mod_sources_. Inter-mod-source routing (cv_in consuming another
 * mod_source's output) requires graph executor Phase 17 changes. Until then,
 * cv_out is computed from scratch each block using the scale parameter only.
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

    void reset() override {}

    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "scale") { scale_ = value; return true; }
        return false;
    }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /*ctx*/ = nullptr) override {
        // Without Phase 17 inter-mod routing, we have no input to consume here.
        // Fill with scale * 1.0 (neutral CV) as a placeholder so bake() passes.
        for (auto& s : output) s = scale_;
    }

private:
    float scale_ = -1.0f;
};

} // namespace audio

#endif // INVERTER_PROCESSOR_HPP
