/**
 * @file AlsaDriver.cpp
 * @brief Linux ALSA implementation of the AudioDriver interface.
 */

#include "AlsaDriver.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <span>

namespace hal {

AlsaDriver::AlsaDriver(int sample_rate, int block_size, int num_channels, const std::string& device)
    : pcm_handle_(nullptr)
    , device_name_(device)
    , sample_rate_(sample_rate)
    , block_size_(block_size)
    , num_channels_(num_channels)
    , running_(false)
{
    float_buffer_.resize(block_size_);
    // s16_buffer will be resized after PCM setup reveals actual channel count
}

AlsaDriver::~AlsaDriver() {
    stop();
}

void AlsaDriver::set_callback(AudioCallback callback) {
    callback_ = callback;
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

    if ((err = snd_pcm_hw_params_set_format(pcm_handle_, hw_params, SND_PCM_FORMAT_S16_LE)) < 0) {
        std::cerr << "ALSA: Cannot set sample format (" << snd_strerror(err) << ")" << std::endl;
        return false;
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

    // Aim for 4 periods for stability
    unsigned int periods = 4;
    snd_pcm_hw_params_set_periods_near(pcm_handle_, hw_params, &periods, 0);

    if ((err = snd_pcm_hw_params(pcm_handle_, hw_params)) < 0) {
        std::cerr << "ALSA: Cannot set parameters (" << snd_strerror(err) << ")" << std::endl;
        return false;
    }

    snd_pcm_hw_params_free(hw_params);

    // Finalize internal buffers
    float_buffer_.assign(block_size_, 0.0f);
    s16_buffer_.assign(block_size_ * num_channels_, 0);

    if ((err = snd_pcm_prepare(pcm_handle_)) < 0) {
        std::cerr << "ALSA: Cannot prepare audio interface for use (" << snd_strerror(err) << ")" << std::endl;
        return false;
    }

    return true;
}

void AlsaDriver::thread_loop() {
    while (running_) {
        if (callback_) {
            callback_(std::span<float>(float_buffer_));

            // Interleave and Convert float to S16_LE
            // Since the graph is mono, we duplicate to all hardware channels
            for (size_t i = 0; i < float_buffer_.size(); ++i) {
                float sample = std::clamp(float_buffer_[i], -1.0f, 1.0f);
                int16_t s16_sample = static_cast<int16_t>(sample * 32767.0f);
                
                for (int ch = 0; ch < num_channels_; ++ch) {
                    s16_buffer_[i * num_channels_ + ch] = s16_sample;
                }
            }

            int err = snd_pcm_writei(pcm_handle_, s16_buffer_.data(), block_size_);
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
        // Underrun
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
