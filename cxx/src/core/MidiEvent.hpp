#pragma once

#include <cstdint>

namespace engine::core {

/**
 * @brief Represents a lightweight, RT-safe MIDI event.
 */
struct MidiEvent {
    uint8_t status;       ///< MIDI status byte (e.g., 0x90 for Note On)
    uint8_t data1;        ///< First data byte (e.g., pitch)
    uint8_t data2;        ///< Second data byte (e.g., velocity)
    uint32_t sampleOffset; ///< Offset in samples from the start of the current audio block

    /**
     * @brief Helper to check if this is a Note On event.
     */
    bool isNoteOn() const {
        return (status & 0xF0) == 0x90 && data2 > 0;
    }

    /**
     * @brief Helper to check if this is a Note Off event.
     * Note: Handles both explicit Note Off (0x80) and Note On with velocity 0.
     */
    bool isNoteOff() const {
        if ((status & 0xF0) == 0x80) return true;
        if ((status & 0xF0) == 0x90 && data2 == 0) return true;
        return false;
    }

    /**
     * @brief Gets the MIDI channel (0-15).
     */
    uint8_t getChannel() const {
        return status & 0x0F;
    }
};

} // namespace engine::core
