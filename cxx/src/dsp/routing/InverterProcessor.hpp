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
#include "../SmoothedParam.hpp"
#include <algorithm>

namespace audio {

class InverterProcessor : public Processor {
public:
    static constexpr float kRampSeconds = 0.010f;

    explicit InverterProcessor(int sample_rate = 48000);

    void reset() override { injected_ = {}; }

    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

    bool apply_parameter(const std::string& name, float value) override {
        // scale_ snaps immediately — it is a patch-configuration parameter.
        if (name == "scale") { scale_.set_target(value, 0); return true; }
        return false;
    }

    void inject_cv(std::string_view port_name, std::span<const float> cv) override {
        if (port_name == "cv_in") injected_ = cv;
    }

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

private:
    int ramp_samples_ = 480;
    SmoothedParam scale_{-1.0f};
    std::span<const float> injected_;
};

} // namespace audio

#endif // INVERTER_PROCESSOR_HPP
