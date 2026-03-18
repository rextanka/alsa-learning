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
#include <cmath>
#include <span>

namespace audio {

class FreeverbProcessor : public Processor {
public:
    static constexpr float kRampSeconds = 0.010f; // 10 ms

    explicit FreeverbProcessor(int sample_rate) : sample_rate_(sample_rate) {
        ramp_samples_ = static_cast<int>(static_cast<float>(sample_rate) * kRampSeconds);
        declare_port({"audio_in",  PORT_AUDIO, PortDirection::IN});
        declare_port({"audio_out", PORT_AUDIO, PortDirection::OUT});
        declare_parameter({"room_size", "Room Size",  0.0f, 1.0f, 0.5f});
        declare_parameter({"damping",   "Damping",    0.0f, 1.0f, 0.5f});
        declare_parameter({"width",     "Width",      0.0f, 1.0f, 1.0f});
        declare_parameter({"wet",       "Wet/Dry",    0.0f, 1.0f, 0.33f});
        init_filters();
    }

    void reset() override {
        for (int i = 0; i < N_COMB; ++i) { comb_l_[i].clear(); comb_r_[i].clear(); }
        for (int i = 0; i < N_AP;   ++i) { ap_l_[i].clear();   ap_r_[i].clear();   }
    }

    bool apply_parameter(const std::string& name, float value) override {
        if (name == "room_size") { room_size_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_); update_params(); return true; }
        if (name == "damping")   { damping_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_); update_params(); return true; }
        if (name == "width")     { width_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_); return true; }
        if (name == "wet")       { wet_.set_target(std::clamp(value, 0.0f, 1.0f), ramp_samples_); return true; }
        return false;
    }

    PortType output_port_type() const override { return PORT_AUDIO; }

protected:
    void do_pull(std::span<float>, const VoiceContext* = nullptr) override {}

    void do_pull(AudioBuffer& buf, const VoiceContext* = nullptr) override {
        const int n = static_cast<int>(buf.frames());
        room_size_.advance(n);
        damping_.advance(n);
        width_.advance(n);
        wet_.advance(n);
        // Recompute comb filter parameters if room_size or damping changed
        if (room_size_.is_ramping() || damping_.is_ramping()) {
            update_params();
        }

        const float wet_val  = wet_.get();
        const float width_val = width_.get();
        const float wet1 = wet_val * (width_val * 0.5f + 0.5f);
        const float wet2 = wet_val * (0.5f - width_val * 0.5f);
        const float dry  = 1.0f - wet_val;
        const size_t frames = buf.frames();

        for (size_t i = 0; i < frames; ++i) {
            const float inL = buf.left[i];
            const float inR = buf.right[i];
            const float mono = (inL + inR) * kInputGain;

            float outL = 0.0f, outR = 0.0f;
            for (int c = 0; c < N_COMB; ++c) {
                outL += comb_l_[c].process(mono);
                outR += comb_r_[c].process(mono);
            }
            for (int a = 0; a < N_AP; ++a) {
                outL = ap_l_[a].process(outL);
                outR = ap_r_[a].process(outR);
            }

            buf.left[i]  = outL * wet1 + outR * wet2 + inL * dry;
            buf.right[i] = outR * wet1 + outL * wet2 + inR * dry;
        }
    }

private:
    static constexpr int   N_COMB         = 8;
    static constexpr int   N_AP           = 4;
    static constexpr float kInputGain     = 0.015f; // prevents overload from 8 parallel combs
    static constexpr float kAllPassGain   = 0.5f;
    static constexpr int   kStereoSpread  = 23;

    // Standard Freeverb delay lengths at 44100 Hz
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

    void init_filters() {
        const float scale = static_cast<float>(sample_rate_) / 44100.0f;
        for (int i = 0; i < N_COMB; ++i) {
            comb_l_[i].init(std::max(static_cast<int>(kCombBase[i] * scale + 0.5f), 4));
            comb_r_[i].init(std::max(static_cast<int>((kCombBase[i] + kStereoSpread) * scale + 0.5f), 4));
        }
        for (int i = 0; i < N_AP; ++i) {
            int len = std::max(static_cast<int>(kApBase[i] * scale + 0.5f), 4);
            ap_l_[i].init(len);
            ap_r_[i].init(len);
        }
        update_params();
    }

    void update_params() {
        const float fb   = room_size_.get() * 0.28f + 0.70f;  // 0.70..0.98
        const float damp = damping_.get();
        for (int i = 0; i < N_COMB; ++i) {
            comb_l_[i].set_feedback(fb);
            comb_r_[i].set_feedback(fb);
            comb_l_[i].set_damp(damp);
            comb_r_[i].set_damp(damp);
        }
    }

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
