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
#include <string>
#include <vector>
#include "AudioBuffer.hpp"
#include "HostDeviceInfo.hpp"

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
     * @brief Callback function type for mono audio processing.
     */
    using AudioCallback = std::function<void(std::span<float> output)>;

    /**
     * @brief Callback function type for stereo audio processing.
     */
    using StereoAudioCallback = std::function<void(audio::AudioBuffer& output)>;

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
     * @brief Set the mono processing callback.
     */
    virtual void set_callback(AudioCallback callback) = 0;

    /**
     * @brief Set the stereo processing callback.
     */
    virtual void set_stereo_callback(StereoAudioCallback callback) = 0;

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

    /**
     * @brief Get the count of buffer under-runs (Xruns) since start.
     * @return int Xrun count (default 0 if unsupported).
     */
    virtual int get_xrun_count() const { return 0; }

    /**
     * @brief Name of the currently open hardware device.
     * Returns a human-readable string (e.g. "MacBook Pro Speakers",
     * "HDA Intel PCH: ALC256 Analog").
     */
    virtual std::string device_name() const { return "Unknown"; }

    /**
     * @brief Enumerate all output-capable audio devices on this host.
     *
     * Declared here; defined in the platform-specific .cpp (CoreAudioDriver.cpp
     * on Apple, AlsaDriver.cpp on Linux). No platform headers are required by
     * callers — the bridge calls this without any OS-specific includes.
     *
     * Returns an empty vector if enumeration fails or no devices are found.
     */
    static std::vector<HostDeviceInfo> enumerate_devices();
};

} // namespace hal

#endif // AUDIO_DRIVER_HPP
