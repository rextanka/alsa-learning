/**
 * @file AlsaDriver.cpp
 * @brief Linux ALSA implementation of the AudioDriver interface.
 */

#include "AlsaDriver.hpp"
#include "../../core/Logger.hpp"
#include "../../core/AudioSettings.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <span>
#include <chrono>
#include <pthread.h>

namespace hal {

AlsaDriver::AlsaDriver(int sample_rate, int block_size, int num_channels, const std::string& device)
    : pcm_handle_(nullptr)
    , device_name_(device)
    , sample_rate_(sample_rate)
    , block_size_(block_size)
    , num_channels_(num_channels)
    , running_(false)
{
    // Buffers will be resized after PCM setup
}

AlsaDriver::~AlsaDriver() {
    stop();
}

void AlsaDriver::set_callback(AudioCallback callback) {
    callback_ = callback;
}

void AlsaDriver::set_stereo_callback(StereoAudioCallback callback) {
    stereo_callback_ = callback;
}

bool AlsaDriver::start() {
    if (running_) return true;

    if (!setup_pcm()) {
        return false;
    }

    running_ = true;
    processing_thread_ = std::thread(&AlsaDriver::thread_loop, this);
    
    return true;
}

void AlsaDriver::stop() {
    running_ = false;
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }

    if (pcm_handle_) {
        snd_pcm_close(pcm_handle_);
        pcm_handle_ = nullptr;
    }
}

bool AlsaDriver::setup_pcm() {
    int err;
    snd_pcm_hw_params_t* hw_params;

    if ((err = snd_pcm_open(&pcm_handle_, device_name_.c_str(), SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        std::cerr << "ALSA: Cannot open audio device " << device_name_ << " (" << snd_strerror(err) << ")" << std::endl;
        return false;
    }

    if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
        std::cerr << "ALSA: Cannot allocate hardware parameter structure (" << snd_strerror(err) << ")" << std::endl;
        return false;
    }

    if ((err = snd_pcm_hw_params_any(pcm_handle_, hw_params)) < 0) {
        std::cerr << "ALSA: Cannot initialize hardware parameter structure (" << snd_strerror(err) << ")" << std::endl;
        return false;
    }

    if ((err = snd_pcm_hw_params_set_access(pcm_handle_, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        std::cerr << "ALSA: Cannot set access type (" << snd_strerror(err) << ")" << std::endl;
        return false;
    }

    // Use S32_LE for high resolution on modern hardware (e.g., Razer laptop)
    if ((err = snd_pcm_hw_params_set_format(pcm_handle_, hw_params, SND_PCM_FORMAT_S32_LE)) < 0) {
        std::cerr << "ALSA: Cannot set S32_LE, falling back to S16_LE" << std::endl;
        if ((err = snd_pcm_hw_params_set_format(pcm_handle_, hw_params, SND_PCM_FORMAT_S16_LE)) < 0) {
            std::cerr << "ALSA: Cannot set sample format (" << snd_strerror(err) << ")" << std::endl;
            return false;
        }
    }

    unsigned int rate = sample_rate_;
    if ((err = snd_pcm_hw_params_set_rate_near(pcm_handle_, hw_params, &rate, 0)) < 0) {
        std::cerr << "ALSA: Cannot set sample rate (" << snd_strerror(err) << ")" << std::endl;
        return false;
    }
    sample_rate_ = rate;

    unsigned int channels = num_channels_;
    if ((err = snd_pcm_hw_params_set_channels_near(pcm_handle_, hw_params, &channels)) < 0) {
        std::cerr << "ALSA: Cannot set channel count (" << snd_strerror(err) << ")" << std::endl;
        return false;
    }
    num_channels_ = static_cast<int>(channels);

    snd_pcm_uframes_t frames = block_size_;
    if ((err = snd_pcm_hw_params_set_period_size_near(pcm_handle_, hw_params, &frames, 0)) < 0) {
        std::cerr << "ALSA: Cannot set period size (" << snd_strerror(err) << ")" << std::endl;
        return false;
    }
    block_size_ = static_cast<int>(frames);

    unsigned int periods = 4;
    snd_pcm_hw_params_set_periods_near(pcm_handle_, hw_params, &periods, 0);

    if ((err = snd_pcm_hw_params(pcm_handle_, hw_params)) < 0) {
        std::cerr << "ALSA: Cannot set parameters (" << snd_strerror(err) << ")" << std::endl;
        return false;
    }

    snd_pcm_hw_params_free(hw_params);

    // Update global settings with actual hardware values
    auto& settings = audio::AudioSettings::instance();
    settings.sample_rate = sample_rate_;
    settings.block_size = block_size_;
    settings.num_channels = num_channels_;

    // Resize internal buffers
    left_buffer_.assign(block_size_, 0.0f);
    right_buffer_.assign(block_size_, 0.0f);
    
    // Determine byte size per frame for interleaved buffer
    snd_pcm_format_t format;
    snd_pcm_hw_params_get_format(hw_params, &format); // This might not work after free, but we know the formats.
    // Re-query format if needed, but we'll assume the one we set.

    interleaved_buffer_.assign(block_size_ * num_channels_ * 4, 0); // Use 4 bytes per sample (S32)

    if ((err = snd_pcm_prepare(pcm_handle_)) < 0) {
        std::cerr << "ALSA: Cannot prepare audio interface for use (" << snd_strerror(err) << ")" << std::endl;
        return false;
    }

    return true;
}

void AlsaDriver::thread_loop() {
    // Set Real-Time Priority (SCHED_FIFO, Priority 80)
    struct sched_param param;
    param.sched_priority = 80;
    int res = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    if (res != 0) {
        if (res == EPERM) {
            audio::AudioLogger::instance().log_message("ALSA", "Priority Failed: EPERM (Need ulimit -r 80+)");
        } else {
            audio::AudioLogger::instance().log_message("ALSA", "Priority Failed: Unknown Error");
        }
    } else {
        audio::AudioLogger::instance().log_message("ALSA", "Real-Time Priority Set (SCHED_FIFO, 80)");
    }

    // RT-Safe: Pre-allocate float buffer for interleaved callback
    std::vector<float> float_interleaved(block_size_ * num_channels_, 0.0f);

    while (running_) {
        // Zeroing: Kill 'zombie data' clicks by ensuring ALL internal buffers are silent
        // before they are filled or converted.
        std::fill(float_interleaved.begin(), float_interleaved.end(), 0.0f);

        if (interleaved_callback_) {
            // Dynamic Capacity: Ensure buffer is large enough for current hardware state
            // (In practice, block_size_ is stable once started, but safety first)
            const size_t required_size = static_cast<size_t>(block_size_ * num_channels_);
            if (float_interleaved.size() < required_size) {
                float_interleaved.resize(required_size, 0.0f);
            }

            // Zeroing: Kill 'zombie data' clicks by ensuring gaps are silent
            std::fill(float_interleaved.begin(), float_interleaved.end(), 0.0f);

            auto start_time = std::chrono::high_resolution_clock::now();

            // Direct float buffer for interleaved output
            interleaved_callback_(std::span<float>(float_interleaved));

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
            audio::AudioLogger::instance().log_event("PROC_US", static_cast<float>(duration));

            // Convert to S32_LE
            int32_t* s32_ptr = reinterpret_cast<int32_t*>(interleaved_buffer_.data());
            for (size_t i = 0; i < float_interleaved.size(); ++i) {
                float sample = std::clamp(float_interleaved[i], -1.0f, 1.0f);
                s32_ptr[i] = static_cast<int32_t>(sample * 2147483647.0f);
            }

            int err = snd_pcm_writei(pcm_handle_, interleaved_buffer_.data(), block_size_);
            if (err < 0) {
                recover_pcm(err);
            }
        } else if (stereo_callback_ || callback_) {
            auto start_time = std::chrono::high_resolution_clock::now();

            audio::AudioBuffer buffer;
            buffer.left = std::span<float>(left_buffer_);
            buffer.right = std::span<float>(right_buffer_);

            if (stereo_callback_) {
                stereo_callback_(buffer);
            } else {
                callback_(buffer.left);
                std::copy(buffer.left.begin(), buffer.left.end(), buffer.right.begin());
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
            audio::AudioLogger::instance().log_event("PROC_US", static_cast<float>(duration));

            // Interleave and Convert to S32_LE (High Res)
            int32_t* s32_ptr = reinterpret_cast<int32_t*>(interleaved_buffer_.data());
            for (size_t i = 0; i < left_buffer_.size(); ++i) {
                float left = std::clamp(left_buffer_[i], -1.0f, 1.0f);
                float right = std::clamp(right_buffer_[i], -1.0f, 1.0f);
                
                s32_ptr[i * num_channels_ + 0] = static_cast<int32_t>(left * 2147483647.0f);
                if (num_channels_ > 1) {
                    s32_ptr[i * num_channels_ + 1] = static_cast<int32_t>(right * 2147483647.0f);
                }
            }

            int err = snd_pcm_writei(pcm_handle_, interleaved_buffer_.data(), block_size_);
            if (err < 0) {
                recover_pcm(err);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void AlsaDriver::recover_pcm(int err) {
    if (err == -EPIPE) {
        snd_pcm_prepare(pcm_handle_);
    } else if (err == -ESTRPIPE) {
        while ((err = snd_pcm_resume(pcm_handle_)) == -EAGAIN)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (err < 0) {
            snd_pcm_prepare(pcm_handle_);
        }
    }
}

} // namespace hal
