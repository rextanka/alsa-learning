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
 * 
 * Note: Current AudioGraph implementation is mono-centric. 
 * This driver supports mono (1 channel) and stereo (2 channels) hardware.
 * For stereo hardware, it will duplicate the mono signal to both channels 
 * (interleaving L, R) if the graph only provides one channel.
 */
class AlsaDriver : public AudioDriver {
public:
    /**
     * @param sample_rate Requested sample rate.
     * @param block_size Requested period size (frames per interrupt).
     * @param num_channels Requested hardware channels (1 for mono, 2 for stereo).
     * @param device ALSA device name.
     */
    AlsaDriver(int sample_rate = 44100, int block_size = 512, int num_channels = 2, const std::string& device = "default");
    ~AlsaDriver() override;

    bool start() override;
    void stop() override;
    void set_callback(AudioCallback callback) override;
    int sample_rate() const override { return sample_rate_; }
    int block_size() const override { return block_size_; }
    int channels() const { return num_channels_; }

private:
    void thread_loop();
    bool setup_pcm();
    void recover_pcm(int err);

    snd_pcm_t* pcm_handle_;
    std::string device_name_;
    int sample_rate_;
    int block_size_;
    int num_channels_;
    AudioCallback callback_;
    std::atomic<bool> running_;
    std::thread processing_thread_;
    
    // Internal buffers
    std::vector<float> float_buffer_;    // Mono input from graph
    std::vector<int16_t> s16_buffer_;    // Interleaved output to hardware
};

} // namespace hal

#endif // HAL_ALSA_DRIVER_HPP
