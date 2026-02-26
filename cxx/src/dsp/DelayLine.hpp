/**
 * @file DelayLine.hpp
 * @brief Simple delay line processor with feedback.
 */

#ifndef AUDIO_DELAY_LINE_HPP
#define AUDIO_DELAY_LINE_HPP

#include "Processor.hpp"
#include <vector>
#include <algorithm>

namespace audio {

/**
 * @brief Simple mono delay line with feedback.
 */
class DelayLine : public Processor {
public:
    /**
     * @param sample_rate Sample rate in Hz.
     * @param max_delay_seconds Maximum delay time in seconds.
     */
    explicit DelayLine(int sample_rate, float max_delay_seconds = 2.0f)
        : sample_rate_(sample_rate)
        , delay_time_(0.5f)
        , feedback_(0.3f)
        , mix_(0.5f)
        , write_pos_(0)
    {
        size_t size = static_cast<size_t>(sample_rate_ * max_delay_seconds);
        buffer_.resize(size, 0.0f);
    }

    void set_delay_time(float seconds) {
        delay_time_ = std::clamp(seconds, 0.001f, static_cast<float>(buffer_.size()) / sample_rate_);
    }

    void set_feedback(float feedback) {
        feedback_ = std::clamp(feedback, 0.0f, 0.99f);
    }

    void set_mix(float mix) {
        mix_ = std::clamp(mix, 0.0f, 1.0f);
    }

    /**
     * @brief Process a single sample through the delay line.
     * 
     * @param input Input sample.
     * @return float Processed sample (mixed wet/dry).
     */
    float process_sample(float input) {
        const float delay_samples = delay_time_ * sample_rate_;
        const size_t buf_size = buffer_.size();

        // Read from delay line with linear interpolation
        float read_pos = static_cast<float>(write_pos_) - delay_samples;
        while (read_pos < 0) read_pos += buf_size;

        size_t i0 = static_cast<size_t>(read_pos) % buf_size;
        size_t i1 = (i0 + 1) % buf_size;
        float frac = read_pos - static_cast<float>(static_cast<size_t>(read_pos));

        float delayed_sample = buffer_[i0] + frac * (buffer_[i1] - buffer_[i0]);

        // Write back to delay line with feedback
        buffer_[write_pos_] = input + (delayed_sample * feedback_);
        write_pos_ = (write_pos_ + 1) % buf_size;

        // Mix wet/dry
        return (input * (1.0f - mix_)) + (delayed_sample * mix_);
    }

    void reset() override {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        write_pos_ = 0;
    }

protected:
    void do_pull(std::span<float> output, const VoiceContext* /* voice_context */ = nullptr) override {
        const float delay_samples = delay_time_ * sample_rate_;
        const size_t buf_size = buffer_.size();

        for (auto& sample : output) {
            float input = sample;

            // Read from delay line (simple linear read, no interpolation for now)
            float read_pos = static_cast<float>(write_pos_) - delay_samples;
            while (read_pos < 0) read_pos += buf_size;
            
            size_t i0 = static_cast<size_t>(read_pos) % buf_size;
            float delayed_sample = buffer_[i0];

            // Write back to delay line with feedback
            buffer_[write_pos_] = input + (delayed_sample * feedback_);
            write_pos_ = (write_pos_ + 1) % buf_size;

            // Mix wet/dry
            sample = (input * (1.0f - mix_)) + (delayed_sample * mix_);
        }
    }

    void do_pull(AudioBuffer& output, const VoiceContext* voice_context = nullptr) override {
        // For simplicity, process stereo input as mono delay
        // In a real delay, we might have two buffers.
        for (size_t i = 0; i < output.frames(); ++i) {
            float input = (output.left[i] + output.right[i]) * 0.5f;
            float processed = input;

            const float delay_samples = delay_time_ * sample_rate_;
            const size_t buf_size = buffer_.size();

            float read_pos = static_cast<float>(write_pos_) - delay_samples;
            while (read_pos < 0) read_pos += buf_size;
            
            size_t i0 = static_cast<size_t>(read_pos) % buf_size;
            float delayed_sample = buffer_[i0];

            buffer_[write_pos_] = input + (delayed_sample * feedback_);
            write_pos_ = (write_pos_ + 1) % buf_size;

            processed = (input * (1.0f - mix_)) + (delayed_sample * mix_);

            output.left[i] = processed;
            output.right[i] = processed;
        }
    }

private:
    int sample_rate_;
    float delay_time_;
    float feedback_;
    float mix_;
    std::vector<float> buffer_;
    size_t write_pos_;
};

} // namespace audio

#endif // AUDIO_DELAY_LINE_HPP
