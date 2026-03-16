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
    WhiteNoiseProcessor() : state_(0xDEADBEEFu) {}

    void reset() override { state_ = 0xDEADBEEFu; }

    PortType output_port_type() const override { return PortType::PORT_AUDIO; }

    // Single-sample accessor for use in per-sample mix loops.
    float tick() {
        state_ = state_ * 1664525u + 1013904223u; // Knuth LCG
        return static_cast<float>(static_cast<int32_t>(state_))
               / static_cast<float>(std::numeric_limits<int32_t>::max());
    }

protected:
    void do_pull(std::span<float> output,
                 const VoiceContext* /* context */ = nullptr) override {
        for (auto& s : output) s = tick();
    }

private:
    uint32_t state_;
};

} // namespace audio

#endif // WHITE_NOISE_PROCESSOR_HPP
