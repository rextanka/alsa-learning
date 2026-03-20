/**
 * @file LadderVcfBase.cpp
 * @brief FM-aware do_pull implementation for 4-pole ladder filters.
 */
#include "LadderVcfBase.hpp"
#include <cmath>

namespace audio {

void LadderVcfBase::do_pull(std::span<float> output, const VoiceContext* ctx) {
    fm_depth_.advance(static_cast<int>(output.size()));
    const float fm_depth_val = fm_depth_.get();
    if (fm_in_.empty() || fm_depth_val == 0.0f) {
        VcfBase::do_pull(output, ctx);
    } else {
        for (size_t i = 0; i < output.size(); ++i) {
            const float eff_cv = cutoff_cv_ + kybd_cv_ + fm_depth_val * fm_in_[i];
            const float fc = std::max(20.0f, base_cutoff_.get() * std::pow(2.0f, eff_cv));
            update_cutoff_coefficient(fc);
            process_sample(output[i]);
        }
        // Restore coefficient to block-rate value after FM loop.
        update_effective_cutoff();
        fm_in_ = {};
    }
}

} // namespace audio
