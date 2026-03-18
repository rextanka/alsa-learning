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
    snd_pcm_hw_params_get_format(hw_params, &format);
    std::string format_msg = "Final Negotiated Format: " + std::to_string(format);
    audio::AudioLogger::instance().log_message("ALSA", format_msg.c_str());

    // Zero out on creation
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

            // Convert to S32_LE with strict clamping to prevent wrap-around
            int32_t* s32_ptr = reinterpret_cast<int32_t*>(interleaved_buffer_.data());
            for (size_t i = 0; i < float_interleaved.size(); ++i) {
                float sample = std::clamp(float_interleaved[i], -1.0f, 1.0f);
                // Clamp to prevent integer overflow before casting
                double scaled = std::clamp(static_cast<double>(sample) * 2147483647.0, -2147483648.0, 2147483647.0);
                s32_ptr[i] = static_cast<int32_t>(scaled);
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

            // Interleave and Convert to S32_LE (High Res) with strict clamping
            int32_t* s32_ptr = reinterpret_cast<int32_t*>(interleaved_buffer_.data());
            for (size_t i = 0; i < left_buffer_.size(); ++i) {
                float left = std::clamp(left_buffer_[i], -1.0f, 1.0f);
                float right = std::clamp(right_buffer_[i], -1.0f, 1.0f);
                
                double scaled_l = std::clamp(static_cast<double>(left) * 2147483647.0, -2147483648.0, 2147483647.0);
                s32_ptr[i * num_channels_ + 0] = static_cast<int32_t>(scaled_l);
                if (num_channels_ > 1) {
                    double scaled_r = std::clamp(static_cast<double>(right) * 2147483647.0, -2147483648.0, 2147483647.0);
                    s32_ptr[i * num_channels_ + 1] = static_cast<int32_t>(scaled_r);
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
        xrun_count_++;
        snd_pcm_prepare(pcm_handle_);
    } else if (err == -ESTRPIPE) {
        while ((err = snd_pcm_resume(pcm_handle_)) == -EAGAIN)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (err < 0) {
            snd_pcm_prepare(pcm_handle_);
        }
    }
}

// ---------------------------------------------------------------------------
// AudioDriver::enumerate_devices — ALSA implementation
// ---------------------------------------------------------------------------

std::vector<HostDeviceInfo> AudioDriver::enumerate_devices() {
    std::vector<HostDeviceInfo> devices;

#ifdef __linux__
    // Common sample rates and power-of-2 period sizes to probe
    static constexpr unsigned int kProbeRates[] = {
        22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000
    };
    static constexpr int kProbeSizes[] = {32, 64, 128, 256, 512, 1024, 2048, 4096};

    int card = -1;
    int idx  = 0;
    while (snd_card_next(&card) == 0 && card >= 0) {
        char ctl_name[32];
        std::snprintf(ctl_name, sizeof(ctl_name), "hw:%d", card);

        snd_ctl_t* ctl = nullptr;
        if (snd_ctl_open(&ctl, ctl_name, 0) < 0) continue;

        snd_ctl_card_info_t* card_info = nullptr;
        snd_ctl_card_info_alloca(&card_info);
        snd_ctl_card_info(ctl, card_info);
        const char* card_name = snd_ctl_card_info_get_name(card_info);

        int dev = -1;
        while (snd_ctl_pcm_next_device(ctl, &dev) == 0 && dev >= 0) {
            snd_pcm_info_t* pcm_info = nullptr;
            snd_pcm_info_alloca(&pcm_info);
            snd_pcm_info_set_device(pcm_info, static_cast<unsigned int>(dev));
            snd_pcm_info_set_subdevice(pcm_info, 0);
            snd_pcm_info_set_stream(pcm_info, SND_PCM_STREAM_PLAYBACK);
            if (snd_ctl_pcm_info(ctl, pcm_info) < 0) continue;

            const char* dev_name = snd_pcm_info_get_name(pcm_info);

            // Probe capabilities via a non-blocking open
            char pcm_id[64];
            std::snprintf(pcm_id, sizeof(pcm_id), "hw:%d,%d", card, dev);
            snd_pcm_t* pcm = nullptr;

            std::vector<int> supported_rates;
            std::vector<int> supported_sizes;
            int default_rate = 48000;
            int default_size = 512;

            if (snd_pcm_open(&pcm, pcm_id, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) == 0) {
                snd_pcm_hw_params_t* params = nullptr;
                snd_pcm_hw_params_alloca(&params);
                if (snd_pcm_hw_params_any(pcm, params) == 0) {
                    for (unsigned int rate : kProbeRates) {
                        if (snd_pcm_hw_params_test_rate(pcm, params, rate, 0) == 0)
                            supported_rates.push_back(static_cast<int>(rate));
                    }
                    for (int sz : kProbeSizes) {
                        snd_pcm_uframes_t frames = static_cast<snd_pcm_uframes_t>(sz);
                        if (snd_pcm_hw_params_test_period_size(pcm, params, frames, 0) == 0)
                            supported_sizes.push_back(sz);
                    }
                    // Preferred rate: pick 48000 if supported, else first
                    if (!supported_rates.empty()) {
                        default_rate = 48000;
                        bool has_48k = false;
                        for (int r : supported_rates) if (r == 48000) { has_48k = true; break; }
                        if (!has_48k) default_rate = supported_rates.front();
                    }
                    if (!supported_sizes.empty()) {
                        default_size = 512;
                        bool has_512 = false;
                        for (int s : supported_sizes) if (s == 512) { has_512 = true; break; }
                        if (!has_512) default_size = supported_sizes.front();
                    }
                }
                snd_pcm_close(pcm);
            }

            if (supported_rates.empty()) supported_rates = {44100, 48000};
            if (supported_sizes.empty()) supported_sizes = {128, 256, 512, 1024};

            std::string full_name = std::string(card_name ? card_name : "Card") + ": "
                                  + std::string(dev_name  ? dev_name  : "PCM");
            devices.push_back({idx++, std::move(full_name),
                                default_rate, default_size,
                                std::move(supported_rates),
                                std::move(supported_sizes)});
        }
        snd_ctl_close(ctl);
    }

    // Always include the "default" virtual device (may not have a card entry)
    if (devices.empty()) {
        devices.push_back({0, "default", 48000, 512, {44100, 48000}, {128, 256, 512, 1024}});
    }
#else
    // Non-Linux build (shouldn't be reached; platform guard is in CMake)
    devices.push_back({0, "default", 48000, 512, {44100, 48000}, {128, 256, 512, 1024}});
#endif

    return devices;
}

// ---------------------------------------------------------------------------
// AudioDriver::create — ALSA factory
// ---------------------------------------------------------------------------

std::unique_ptr<AudioDriver> AudioDriver::create(int sample_rate, int block_size) {
    return std::make_unique<AlsaDriver>(sample_rate, block_size);
}

} // namespace hal
