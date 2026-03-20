/**
 * @file CemFilterProcessor.hpp
 * @brief SH-style CEM/IR3109 4-pole low-pass filter, 24 dB/oct.
 *
 * RT-SAFE chain node: PORT_AUDIO in → PORT_AUDIO out.
 * Type name: SH_FILTER
 *
 * Character: clean, liquid, stable self-oscillation — uses algebraic soft-clip
 * `x/(1+|x|)` in the feedback path (gentler than Moog-style tanh) with fully
 * linear stage updates. More open high-end at moderate resonance; suits solid,
 * punchy lead lines and bass patches.
 */

#ifndef CEM_FILTER_PROCESSOR_HPP
#define CEM_FILTER_PROCESSOR_HPP

#include "LadderVcfBase.hpp"

namespace audio {

class CemFilterProcessor : public LadderVcfBase {
public:
    explicit CemFilterProcessor(int sample_rate)
        : LadderVcfBase(sample_rate)
    {}

protected:
    void process_sample(float& sample) override;
};

} // namespace audio

#endif // CEM_FILTER_PROCESSOR_HPP
