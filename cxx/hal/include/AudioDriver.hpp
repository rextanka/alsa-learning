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

namespace hal {

/**
 * @brief Abstract base class for audio hardware drivers.
 * 
 * Platform-specific implementations (ALSA, CoreAudio, WASAPI) will inherit
 * from this class to provide a unified interface for audio I/O.
 */
class AudioDriver {
public:
    virtual ~AudioDriver() = default;

    // TODO: Define pure virtual methods for audio device operations
    // (open, close, write, read, etc.)
};

} // namespace hal

#endif // AUDIO_DRIVER_HPP
