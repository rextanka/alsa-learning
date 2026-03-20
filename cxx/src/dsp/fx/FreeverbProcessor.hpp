/**
 * @file FreeverbProcessor.hpp
 * @brief Schroeder/Freeverb stereo reverb (Jezar at Dreampoint, 1997).
 *
 * Signal flow (per channel):
 *   mono_in → 8× CombFilter (parallel sum) → 4× AllPass (series) → wet out
 *   wet out is mixed with dry input using width-based stereo spread.
 *
 * Stereo: L and R comb filters use different delay lengths (R = L + 23 samples).
 *
 * Parameters:
 *   room_size  0.0–1.0   reverb time  (maps feedback: 0.70–0.98)
 *   damping    0.0–1.0   high-frequency absorption in comb filters
 *   width      0.0–1.0   stereo spread (0=mono, 1=full stereo)
 *   wet        0.0–1.0   wet/dry mix (0=dry, 1=fully wet)
 */

#ifndef FREEVERB_PROCESSOR_HPP
#define FREEVERB_PROCESSOR_HPP

#include "../Processor.hpp"
#include "../SmoothedParam.hpp"
#include <vector>
#include <algorithm>
#include <span>

namespace audio {

class FreeverbProcessor : public Processor {
public:
    static constexpr float kRampSeconds = 0.010f; // 10 ms

    explicit FreeverbProcessor(int sample_rate);

    void reset() override {
        for (int i = 0; i < N_COMB; ++i) { comb_l_[i].clear(); comb_r_[i].clear(); }
        for (int i = 0; i < N_AP;   ++i) { ap_l_[i].clear();   ap_r_[i].clear();   }
    }

    bool apply_parameter(const std::string& name, float value) override;

    PortType output_port_type() const override { return PORT_AUDIO; }

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx = nullptr) override;
    void do_pull(AudioBuffer& buf, const VoiceContext* ctx = nullptr) override;

private:
    static constexpr int   N_COMB         = 8;
    static constexpr int   N_AP           = 4;
    static constexpr float kInputGain     = 0.015f;
    static constexpr float kAllPassGain   = 0.5f;
    static constexpr int   kStereoSpread  = 23;

    // Jezar's reference delay lengths (samples at 44100 Hz); scaled to actual sample rate in init_filters().
    static constexpr int kCombBase[N_COMB] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static constexpr int kApBase[N_AP]     = {556, 441, 341, 225};

    // -----------------------------------------------------------------------
    // CombFilter: Schroeder comb with low-pass in the feedback loop
    // -----------------------------------------------------------------------
    struct CombFilter {
        std::vector<float> buf;
        int   pos        = 0;
        float filt_state = 0.0f;
        float feedback   = 0.5f;
        float damp1      = 0.5f;  // = damping
        float damp2      = 0.5f;  // = 1 - damping

        void init(int size) { buf.assign(size, 0.0f); pos = 0; filt_state = 0.0f; }

        float process(float input) {
            float output  = buf[pos];
            filt_state    = output * damp2 + filt_state * damp1;
            buf[pos]      = input + filt_state * feedback;
            if (++pos >= static_cast<int>(buf.size())) pos = 0;
            return output;
        }

        void set_feedback(float fb)  { feedback = fb; }
        void set_damp(float d)       { damp1 = d; damp2 = 1.0f - d; }
        void clear() { std::fill(buf.begin(), buf.end(), 0.0f); filt_state = 0.0f; }
    };

    // -----------------------------------------------------------------------
    // AllPass: Schroeder all-pass diffuser
    // -----------------------------------------------------------------------
    struct AllPass {
        std::vector<float> buf;
        int pos = 0;

        void init(int size) { buf.assign(size, 0.0f); pos = 0; }

        float process(float input) {
            float buf_out = buf[pos];
            buf[pos] = input + buf_out * kAllPassGain;
            if (++pos >= static_cast<int>(buf.size())) pos = 0;
            return buf_out - input;
        }

        void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
    };

    void init_filters();
    void update_params();

    int sample_rate_;
    int ramp_samples_;

    SmoothedParam room_size_{0.5f};
    SmoothedParam damping_{0.5f};
    SmoothedParam width_{1.0f};
    SmoothedParam wet_{0.33f};

    CombFilter comb_l_[N_COMB], comb_r_[N_COMB];
    AllPass    ap_l_[N_AP],     ap_r_[N_AP];
};

} // namespace audio

#endif // FREEVERB_PROCESSOR_HPP
