/**
 * @file AudioDriver.hpp
 * @brief Abstract base class for platform-specific audio hardware drivers.
 * 
 * This file follows the project rules defined in .cursorrules:
 * - Separation of Concerns: Hardware/OS audio code (ALSA, CoreAudio, WASAPI) 
 *   must be strictly separated from core DSP logic.
 * - Modern C++: Target C++20/23 for all new code.
 */

#ifndef AUDIO_DRIVER_HPP
#define AUDIO_DRIVER_HPP

#include <functional>
#include <span>

namespace hal {

/**
 * @brief Abstract base class for audio hardware drivers.
 * 
 * Platform-specific implementations (ALSA, CoreAudio, WASAPI) will inherit
 * from this class to provide a unified interface for audio I/O.
 */
class AudioDriver {
public:
    /**
     * @brief Callback function type for audio processing.
     * 
     * The callback is responsible for filling the output buffer with audio data.
     * @param output The buffer to fill with audio samples (interleaved or mono).
     */
    using AudioCallback = std::function<void(std::span<float> output)>;

    virtual ~AudioDriver() = default;

    /**
     * @brief Start the audio driver.
     * 
     * @return true if successfully started, false otherwise.
     */
    virtual bool start() = 0;

    /**
     * @brief Stop the audio driver.
     */
    virtual void stop() = 0;

    /**
     * @brief Set the processing callback.
     * 
     * @param callback The function to call when the hardware needs more data.
     */
    virtual void set_callback(AudioCallback callback) = 0;

    /**
     * @brief Get the current sample rate.
     * 
     * @return int Sample rate in Hz.
     */
    virtual int sample_rate() const = 0;

    /**
     * @brief Get the current block size (buffer size).
     * 
     * @return int Number of frames per block.
     */
    virtual int block_size() const = 0;
};

} // namespace hal

#endif // AUDIO_DRIVER_HPP
