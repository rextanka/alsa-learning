/**
 * @file WhiteNoiseProcessor.hpp
 * @brief RT-safe white noise generator using a linear congruential generator.
 *
 * Output contract: PORT_AUDIO, range [-1.0, 1.0].
 * No allocations; state is a single uint32_t.
 */

#ifndef WHITE_NOISE_PROCESSOR_HPP
#define WHITE_NOISE_PROCESSOR_HPP

#include "../Processor.hpp"
#include <cstdint>
#include <limits>
#include <span>

namespace audio {

class WhiteNoiseProcessor : public Processor {
public:
    WhiteNoiseProcessor() : state_(0xDEADBEEFu) {
        // Phase 15: named port declarations
        declare_port({"audio_out", PORT_AUDIO, PortDirection::OUT});
        declare_parameter({"color", "Noise Color", 0.0f, 1.0f, 0.0f}); // 0=White, 1=Pink
    }

    void reset() override {
        state_ = 0xDEADBEEFu;
        b0_ = b1_ = b2_ = b3_ = b4_ = b5_ = b6_ = 0.0f;
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "color") {
            color_ = static_cast<int>(std::round(value)); // 0=White, 1=Pink
            return true;
        }
        return false;
    }

    PortType output_port_type() const override { return PortType::PORT_AUDIO; }

    // Single-sample accessor for white noise — used in CompositeGenerator.
    float tick() {
        state_ = state_ * 1664525u + 1013904223u; // Knuth LCG
        return static_cast<float>(static_cast<int32_t>(state_))
               / static_cast<float>(std::numeric_limits<int32_t>::max());
    }

    // Single-sample pink noise using the Paul Kellett −3dB/octave IIR approximation.
    // 7 all-pass stages with poles at approximately log-spaced frequencies.
    float tick_pink() {
        const float white = tick();
        b0_ = 0.99886f * b0_ + white * 0.0555179f;
        b1_ = 0.99332f * b1_ + white * 0.0750759f;
        b2_ = 0.96900f * b2_ + white * 0.1538520f;
        b3_ = 0.86650f * b3_ + white * 0.3104856f;
        b4_ = 0.55000f * b4_ + white * 0.5329522f;
        b5_ = -0.7616f * b5_ - white * 0.0168980f;
        const float pink = (b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + white * 0.5362f)
                           * 0.11f; // normalise to approximately [-1, 1]
        b6_ = white * 0.115926f;
        return pink;
    }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /* context */ = nullptr) override {
        if (color_ == 1) {
            for (auto& s : output) s = tick_pink();
        } else {
            for (auto& s : output) s = tick();
        }
    }

private:
    uint32_t state_;
    int color_ = 0; // 0=White, 1=Pink

    // Pink noise filter state (Paul Kellett 7-pole approximation)
    float b0_ = 0.0f, b1_ = 0.0f, b2_ = 0.0f, b3_ = 0.0f;
    float b4_ = 0.0f, b5_ = 0.0f, b6_ = 0.0f;
};

} // namespace audio

#endif // WHITE_NOISE_PROCESSOR_HPP
