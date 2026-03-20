/**
 * @file WhiteNoiseProcessor.hpp
 * @brief RT-safe white/pink noise generator using a linear congruential generator.
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
    WhiteNoiseProcessor();

    void reset() override;
    bool apply_parameter(const std::string& name, float value) override;
    PortType output_port_type() const override { return PortType::PORT_AUDIO; }

    // Single-sample white noise accessor — used by CompositeGenerator.
    float tick() {
        state_ = state_ * 1664525u + 1013904223u; // Knuth LCG
        return static_cast<float>(static_cast<int32_t>(state_))
               / static_cast<float>(std::numeric_limits<int32_t>::max());
    }

    float tick_pink();

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;

private:
    uint32_t state_;
    int color_ = 0; // 0=White, 1=Pink

    // Pink noise filter state (Paul Kellett 7-pole approximation)
    float b0_ = 0.0f, b1_ = 0.0f, b2_ = 0.0f, b3_ = 0.0f;
    float b4_ = 0.0f, b5_ = 0.0f, b6_ = 0.0f;
};

} // namespace audio

#endif // WHITE_NOISE_PROCESSOR_HPP
