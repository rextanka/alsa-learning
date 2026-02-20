/**
 * @file CoreAudioDriver.hpp
 * @brief macOS CoreAudio implementation of the AudioDriver interface.
 */

#ifndef HAL_COREAUDIO_DRIVER_HPP
#define HAL_COREAUDIO_DRIVER_HPP

#include "../include/AudioDriver.hpp"
#include <AudioToolbox/AudioToolbox.h>
#include <atomic>
#include <vector>

namespace hal {

/**
 * @brief CoreAudio implementation for macOS.
 */
class CoreAudioDriver : public AudioDriver {
public:
    CoreAudioDriver(int sample_rate = 44100, int block_size = 512);
    ~CoreAudioDriver() override;

    bool start() override;
    void stop() override;
    void set_callback(AudioCallback callback) override;
    int sample_rate() const override { return sample_rate_; }
    int block_size() const override { return block_size_; }

private:
    static OSStatus render_callback(
        void* inRefCon,
        AudioUnitRenderActionFlags* ioActionFlags,
        const AudioTimeStamp* inTimeStamp,
        UInt32 inBusNumber,
        UInt32 inNumberFrames,
        AudioBufferList* ioData);

    AudioComponentInstance audio_unit_;
    int sample_rate_;
    int block_size_;
    AudioCallback callback_;
    std::atomic<bool> running_;
    
    // Internal buffer for mono processing if needed
    std::vector<float> mono_buffer_;
};

} // namespace hal

#endif // HAL_COREAUDIO_DRIVER_HPP
