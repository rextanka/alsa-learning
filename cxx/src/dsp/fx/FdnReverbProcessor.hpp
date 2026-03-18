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
#include <cmath>
#include <algorithm>
#include <span>

namespace audio {

class FdnReverbProcessor : public Processor {
public:
    /**
     * @param sample_rate   Audio sample rate in Hz.
     * @param max_room_size Upper bound for the room_size parameter [0.0, 1.0].
     *                      Controls how much memory is pre-allocated for delay
     *                      lines at construction time.  Reduce below 1.0 on
     *                      memory-constrained (embedded) targets.
     *                      Values for the room_size parameter are clamped to
     *                      this ceiling at runtime.
     */
    static constexpr float kRampSeconds = 0.010f; // 10 ms

    explicit FdnReverbProcessor(int sample_rate, float max_room_size = 1.0f)
        : sample_rate_(sample_rate)
        , ramp_samples_(static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds))
        , max_room_size_(std::clamp(max_room_size, 0.05f, 1.0f))
    {
        declare_port({"audio_in",  PORT_AUDIO, PortDirection::IN});
        declare_port({"audio_out", PORT_AUDIO, PortDirection::OUT});
        declare_parameter({"decay",     "Decay (T60 s)",  0.1f, 30.0f,  2.0f, true});
        declare_parameter({"room_size", "Room Size",      0.0f,  1.0f,  0.5f});
        declare_parameter({"damping",   "Damping",        0.0f,  1.0f,  0.3f});
        declare_parameter({"width",     "Stereo Width",   0.0f,  1.0f,  1.0f});
        declare_parameter({"wet",       "Wet/Dry",        0.0f,  1.0f,  0.3f});
        alloc_buffers();
        update_delay_lengths();
        update_gains();
    }

    void reset() override {
        for (int i = 0; i < N; ++i) {
            std::fill(d_state_[i].buf.begin(), d_state_[i].buf.end(), 0.0f);
            d_state_[i].write_pos = 0;
            d_state_[i].lp_state  = 0.0f;
        }
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "decay")     { decay_.set_target(std::max(0.1f, value), ramp_samples_);                     update_gains(); return true; }
        if (name == "room_size") { room_size_ = std::clamp(value, 0.0f, max_room_size_); update_delay_lengths(); update_gains(); return true; } // snap
        if (name == "damping")   { damping_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_);          update_gains(); return true; }
        if (name == "width")     { width_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_);            return true; }
        if (name == "wet")       { wet_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_);              return true; }
        return false;
    }

    PortType output_port_type() const override { return PORT_AUDIO; }

protected:
    void do_pull(std::span<float>, const VoiceContext* = nullptr) override {}

    void do_pull(AudioBuffer& buf, const VoiceContext* = nullptr) override {
        const int n = static_cast<int>(buf.frames());
        decay_.advance(n);
        damping_.advance(n);
        width_.advance(n);
        wet_.advance(n);
        if (decay_.is_ramping() || damping_.is_ramping()) {
            update_gains();
        }

        const float wet_val  = wet_.get();
        const float dry      = 1.0f - wet_val;
        const float out_gain = wet_val * kOutGain;
        const float width_val = width_.get();
        const size_t frames  = buf.frames();

        for (size_t f = 0; f < frames; ++f) {
            const float inL     = buf.left[f];
            const float inR     = buf.right[f];
            const float in_mono = (inL + inR) * kInGain;

            // Read each delay line
            float x[N];
            for (int i = 0; i < N; ++i) {
                const int buf_size = static_cast<int>(d_state_[i].buf.size());
                const int read_pos = ((d_state_[i].write_pos - delay_len_[i]) % buf_size + buf_size) % buf_size;
                x[i] = d_state_[i].buf[read_pos];
            }

            // 1-pole lowpass (frequency-dependent damping)
            for (int i = 0; i < N; ++i) {
                d_state_[i].lp_state = lp_coeff_ * x[i]
                                     + (1.0f - lp_coeff_) * d_state_[i].lp_state;
                x[i] = d_state_[i].lp_state;
            }

            // Per-line T60 feedback gain
            for (int i = 0; i < N; ++i) x[i] *= g_[i];

            // Householder feedback matrix: y[i] = x[i] - (2/N) * sum(x)
            float sum = 0.0f;
            for (int i = 0; i < N; ++i) sum += x[i];
            const float fb_offset = sum * (2.0f / N);
            for (int i = 0; i < N; ++i) x[i] -= fb_offset;

            // Write back (+ input injection)
            for (int i = 0; i < N; ++i) {
                d_state_[i].buf[d_state_[i].write_pos] = x[i] + in_mono;
                if (++d_state_[i].write_pos >= static_cast<int>(d_state_[i].buf.size()))
                    d_state_[i].write_pos = 0;
            }

            // Stereo extraction: even→L, odd→R
            float outL = 0.0f, outR = 0.0f;
            for (int i = 0; i < N; i += 2) outL += x[i];
            for (int i = 1; i < N; i += 2) outR += x[i];

            // Width blend
            const float mono_mix = (outL + outR) * 0.5f;
            outL = mono_mix + (outL - mono_mix) * width_val;
            outR = mono_mix + (outR - mono_mix) * width_val;

            buf.left[f]  = outL * out_gain + inL * dry;
            buf.right[f] = outR * out_gain + inR * dry;
        }
    }

private:
    static constexpr int N = 8;

    // Base delay times in milliseconds (physical, sample-rate independent).
    // Values are mutually prime when converted to nearest integer samples
    // at most standard sample rates (44100, 48000, 88200, 96000 Hz).
    static constexpr float kBaseDelays_ms[N] = {
        14.60f, 17.23f, 21.02f, 24.40f, 27.10f, 30.15f, 33.52f, 36.27f
    };

    // room_size 0→1 maps scale factor to [0.5, 1.5].
    // Pre-allocation uses max_room_size_ (constructor arg) as the ceiling.

    // Output normalization: N/2=4 lines per channel
    static constexpr float kOutGain = 1.0f / 2.5f;
    static constexpr float kInGain  = 0.5f;

    struct DelayState {
        std::vector<float> buf;
        int   write_pos = 0;
        float lp_state  = 0.0f;
    };

    // Allocate buffers sized for the maximum possible delay (room_size=1.0 → scale=1.5)
    // at this processor's sample rate.  Called once at construction.
    void alloc_buffers() {
        const float sr            = static_cast<float>(sample_rate_);
        const float alloc_scale   = 0.5f + max_room_size_ * 1.0f;  // matches update_delay_lengths formula
        for (int i = 0; i < N; ++i) {
            const int max_len = static_cast<int>(kBaseDelays_ms[i] * 0.001f * sr * alloc_scale) + 2;
            d_state_[i].buf.assign(max_len, 0.0f);
            d_state_[i].write_pos = 0;
            d_state_[i].lp_state  = 0.0f;
        }
    }

    void update_delay_lengths() {
        const float sr         = static_cast<float>(sample_rate_);
        const float room_scale = 0.5f + room_size_ * 1.0f;  // [0.5, 1.5]
        for (int i = 0; i < N; ++i) {
            const int max_len = static_cast<int>(d_state_[i].buf.size());
            delay_len_[i] = std::clamp(
                static_cast<int>(kBaseDelays_ms[i] * 0.001f * sr * room_scale),
                8, max_len - 1
            );
        }
    }

    void update_gains() {
        const float sr    = static_cast<float>(sample_rate_);
        const float decay = std::max(decay_.get(), 0.01f);
        for (int i = 0; i < N; ++i) {
            g_[i] = std::pow(10.0f, -3.0f * static_cast<float>(delay_len_[i]) / (decay * sr));
        }
        lp_coeff_ = 1.0f - damping_.get() * 0.85f;  // [0.15, 1.0]
    }

    int   sample_rate_;
    int   ramp_samples_ = 0;
    float max_room_size_ = 1.0f; // ceiling for room_size_, set at construction
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
