/**
 * @file VoiceContext.hpp
 * @brief Interface for querying voice parameters (query pattern).
 * 
 * This file follows the project rules defined in .cursorrules:
 * - Modern C++: Target C++20/23 for all new code.
 * - Per-voice parameter control: Core requirement for MIDI support.
 * - Query pattern: Processors query voice parameters when needed.
 */

#ifndef VOICE_CONTEXT_HPP
#define VOICE_CONTEXT_HPP

#include <cstdint>

namespace audio {

/**
 * @brief Interface for querying voice parameters.
 * 
 * Processors use this interface to query velocity, aftertouch, and note information
 * from the voice that owns them. This follows the query pattern - processors pull
 * parameters when needed rather than having them pushed.
 */
class VoiceContext {
public:
    virtual ~VoiceContext() = default;

    /**
     * @brief Get the current velocity (0-127).
     */
    virtual uint8_t get_velocity() const = 0;

    /**
     * @brief Get the current aftertouch value (0-127).
     */
    virtual uint8_t get_aftertouch() const = 0;

    /**
     * @brief Get the current note number (MIDI 0-127, or -1 if idle).
     */
    virtual float get_current_note() const = 0;
};

} // namespace audio

#endif // VOICE_CONTEXT_HPP
