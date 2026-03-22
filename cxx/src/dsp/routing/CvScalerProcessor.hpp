/**
 * @file CvScalerProcessor.hpp
 * @brief CV amplifier/attenuverter — multiplies a PORT_CONTROL signal by a scale factor.
 *
 * Use when you need more CV swing than the source naturally produces. A typical
 * ADSR outputs 0–1; routing through a CV_SCALER with scale=4.0 gives 0–4, which
 * drives a 1V/oct VCF cutoff through 4 octaves rather than 1.
 *
 * PORT_CONTROL in:  cv_in
 * PORT_CONTROL out: cv_out  (= cv_in * scale + offset)
 *
 * Parameters:
 *   scale   [-10, 10], default 1.0 — gain applied to the incoming CV
 *   offset  [-5, 5],   default 0.0 — DC added after scaling (shifts centre point)
 *
 * Note: This processor is a mod_source (output_port_type = PORT_CONTROL) and must
 * appear before its downstream consumers in the chain list so Voice::pull_mono
 * processes it first via inject_cv ordering.
 */

#ifndef CV_SCALER_PROCESSOR_HPP
#define CV_SCALER_PROCESSOR_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
#include <algorithm>

namespace audio {

class CvScalerProcessor : public Processor {
public:
    static constexpr float kRampSeconds = 0.010f;

    explicit CvScalerProcessor(int sample_rate = 48000) {
        ramp_samples_ = static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds);
        declare_port({"cv_in",  PORT_CONTROL, PortDirection::IN,  false});
        declare_port({"cv_out", PORT_CONTROL, PortDirection::OUT, false});
        declare_parameter({"scale",  "Scale",  -10.0f, 10.0f, 1.0f});
        declare_parameter({"offset", "Offset",  -5.0f,  5.0f, 0.0f});
    }

    void reset() override { injected_ = {}; }

    PortType output_port_type() const override { return PortType::PORT_CONTROL; }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "scale")  { scale_.set_target(value, 0);  return true; }
        if (name == "offset") { offset_.set_target(value, 0); return true; }
        return false;
    }

    void inject_cv(std::string_view port_name, std::span<const float> cv) override {
        if (port_name == "cv_in") injected_ = cv;
    }

protected:
    void do_pull(std::span<float> output, const VoiceContext* /*ctx*/ = nullptr) override;

private:
    int ramp_samples_ = 480;
    SmoothedParam scale_{1.0f};
    SmoothedParam offset_{0.0f};
    std::span<const float> injected_;
};

} // namespace audio

#endif // CV_SCALER_PROCESSOR_HPP
