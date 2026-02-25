#pragma once

#include "MidiEvent.hpp"
#include <vector>
#include <functional>

namespace engine::core {

/**
 * @brief State-machine based MIDI 1.0 parser.
 * Supports Running Status and multi-byte message reconstruction.
 */
class MidiParser {
public:
    using EventCallback = std::function<void(const MidiEvent&)>;

    MidiParser() = default;

    /**
     * @brief Parses a stream of MIDI bytes.
     * @param data Pointer to the MIDI data.
     * @param size Number of bytes to parse.
     * @param sampleOffset Base sample offset for events in this chunk.
     * @param callback Function to call for each completed MidiEvent.
     */
    void parse(const uint8_t* data, size_t size, uint32_t sampleOffset, EventCallback callback);

private:
    enum class State {
        WaitingForStatus,
        WaitingForData1,
        WaitingForData2
    };

    State m_state = State::WaitingForStatus;
    uint8_t m_runningStatus = 0;
    uint8_t m_pendingStatus = 0;
    uint8_t m_pendingData1 = 0;

    bool isStatusByte(uint8_t byte) const { return byte >= 0x80; }
    int getExpectedDataBytes(uint8_t status) const;
};

} // namespace engine::core
