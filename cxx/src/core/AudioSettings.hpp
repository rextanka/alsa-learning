/**
 * @file AudioSettings.hpp
 * @brief Thread-safe storage for hardware-negotiated audio settings.
 */

#ifndef AUDIO_AUDIO_SETTINGS_HPP
#define AUDIO_AUDIO_SETTINGS_HPP

#include <atomic>

namespace audio {

/**
 * @brief Holds actual hardware settings (Sample Rate, Block Size).
 * 
 * Uses std::atomic to ensure thread-safety between the driver (writer)
 * and the DSP/UI (readers).
 */
struct AudioSettings {
    std::atomic<int> sample_rate{44100};
    std::atomic<int> block_size{512};
    std::atomic<int> num_channels{2};

    // Shared singleton instance for the process
    static AudioSettings& instance() {
        static AudioSettings inst;
        return inst;
    }
};

} // namespace audio

#endif // AUDIO_AUDIO_SETTINGS_HPP
