/**
 * @file FdnReverbProcessor.hpp
 * @brief Jean-Marc Jot Feedback Delay Network (FDN) stereo reverb.
 *
 * Based on: Jot & Chaigne, "Analysis and Synthesis of Room Reverberation
 * Based on a Statistical Time-Frequency Model", ICMC 1992.
 *
 * Architecture:
 *   N=8 delay lines with mutually-prime lengths, connected via a Householder
 *   feedback matrix (H = I - 2/N · 1·1ᵀ). Each feedback path passes through
 *   a 1-pole lowpass for frequency-dependent decay (damping).
 *
 * Delay lengths are specified in milliseconds and converted to samples at
 * construction, so the processor is sample-rate agnostic.  Buffers are
 * pre-allocated at max_room_scale=1.5 so that room_size changes are RT-safe.
 *
 * Per-line feedback gain (exact T60):
 *   g_i = 10^(-3 · d_i / (decay · sr))
 *   → After 'decay' seconds, energy in delay line i has decayed by exactly 60 dB.
 *
 * Householder feedback:
 *   y[i] = x[i] − (2/N) · Σ x[j]
 *   (orthogonal, energy-preserving, maximises modal density)
 *
 * Stereo extraction:
 *   L = mean of even-indexed delay outputs
 *   R = mean of odd-indexed delay outputs
 *
 * Parameters:
 *   decay      0.1–30.0 s  reverberation time (T60)
 *   room_size  0.0–1.0     scales delay lengths (0.5..1.5×)
 *   damping    0.0–1.0     high-frequency absorption
 *   width      0.0–1.0     stereo spread (0=mono, 1=full L/R split)
 *   wet        0.0–1.0     wet/dry mix
 */

#ifndef FDN_REVERB_PROCESSOR_HPP
#define FDN_REVERB_PROCESSOR_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
#include <vector>
#include <algorithm>
#include <span>

namespace audio {

class FdnReverbProcessor : public Processor {
public:
    static constexpr float kRampSeconds = 0.010f; // 10 ms

    explicit FdnReverbProcessor(int sample_rate, float max_room_size = 1.0f);

    void reset() override {
        for (int i = 0; i < N; ++i) {
            std::fill(d_state_[i].buf.begin(), d_state_[i].buf.end(), 0.0f);
            d_state_[i].write_pos = 0;
            d_state_[i].lp_state  = 0.0f;
        }
    }

    bool apply_parameter(const std::string& name, float value) override;

    PortType output_port_type() const override { return PORT_AUDIO; }

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;
    void do_pull(AudioBuffer& buf, const VoiceContext* ctx = nullptr) override;

private:
    static constexpr int N = 8;

    // Base delay times in milliseconds (physical, sample-rate independent).
    // Values are mutually prime when converted to nearest integer samples
    // at most standard sample rates (44100, 48000, 88200, 96000 Hz).
    static constexpr float kBaseDelays_ms[N] = {
        14.60f, 17.23f, 21.02f, 24.40f, 27.10f, 30.15f, 33.52f, 36.27f
    };

    // Output normalization: N/2=4 lines per channel
    static constexpr float kOutGain = 1.0f / 2.5f;
    static constexpr float kInGain  = 0.5f;

    struct DelayState {
        std::vector<float> buf;
        int   write_pos = 0;
        float lp_state  = 0.0f;
    };

    void alloc_buffers();
    void update_delay_lengths();
    void update_gains();

    int   sample_rate_;
    int   ramp_samples_ = 0;
    float max_room_size_ = 1.0f;
    SmoothedParam decay_{2.0f};
    float room_size_ = 0.5f;   // snap — buffer geometry is fixed at construction
    SmoothedParam damping_{0.3f};
    SmoothedParam width_{1.0f};
    SmoothedParam wet_{0.3f};

    int   delay_len_[N]{};
    float g_[N]{};
    float lp_coeff_ = 0.745f;

    DelayState d_state_[N];
};

} // namespace audio

#endif // FDN_REVERB_PROCESSOR_HPP
