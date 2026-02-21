/**
 * @file AlsaDriver.hpp
 * @brief Linux ALSA implementation of the AudioDriver interface.
 */

#ifndef HAL_ALSA_DRIVER_HPP
#define HAL_ALSA_DRIVER_HPP

#include "../include/AudioDriver.hpp"
#include <alsa/asoundlib.h>
#include <atomic>
#include <vector>
#include <thread>
#include <string>
#include <span>

namespace hal {

/**
 * @brief ALSA implementation for Linux.
 */
class AlsaDriver : public AudioDriver {
public:
    AlsaDriver(int sample_rate = 44100, int block_size = 512, const std::string& device = "default");
    ~AlsaDriver() override;

    bool start() override;
    void stop() override;
    void set_callback(AudioCallback callback) override;
    int sample_rate() const override { return sample_rate_; }
    int block_size() const override { return block_size_; }

private:
    void thread_loop();
    bool setup_pcm();
    void recover_pcm(int err);

    snd_pcm_t* pcm_handle_;
    std::string device_name_;
    int sample_rate_;
    int block_size_;
    AudioCallback callback_;
    std::atomic<bool> running_;
    std::thread processing_thread_;
    
    // Internal buffer for float to int conversion
    std::vector<float> float_buffer_;
    std::vector<int16_t> s16_buffer_;
};

} // namespace hal

#endif // HAL_ALSA_DRIVER_HPP
