#include "MidiParser.hpp"

namespace engine::core {

void MidiParser::parse(const uint8_t* data, size_t size, uint32_t sampleOffset, EventCallback callback) {
    for (size_t i = 0; i < size; ++i) {
        uint8_t byte = data[i];

        if (isStatusByte(byte)) {
            // System Real-Time messages (0xF8-0xFF) should be handled independently
            // for sample-accurate timing, but for now we just skip them or handle them
            // if we were building a full sequencer.
            if (byte >= 0xF8) continue; 

            m_pendingStatus = byte;
            m_runningStatus = byte;
            m_state = State::WaitingForData1;
            continue;
        }

        // If we get a data byte but we're in WaitingForStatus, it's Running Status
        if (m_state == State::WaitingForStatus && m_runningStatus != 0) {
            m_pendingStatus = m_runningStatus;
            m_state = State::WaitingForData1;
        }

        switch (m_state) {
            case State::WaitingForData1:
                m_pendingData1 = byte;
                if (getExpectedDataBytes(m_pendingStatus) == 1) {
                    callback({m_pendingStatus, m_pendingData1, 0, sampleOffset});
                    m_state = State::WaitingForStatus;
                } else {
                    m_state = State::WaitingForData2;
                }
                break;

            case State::WaitingForData2:
                callback({m_pendingStatus, m_pendingData1, byte, sampleOffset});
                m_state = State::WaitingForStatus;
                break;

            case State::WaitingForStatus:
                // Should not happen if m_runningStatus is handled correctly above
                break;
        }
    }
}

int MidiParser::getExpectedDataBytes(uint8_t status) const {
    uint8_t type = status & 0xF0;
    switch (type) {
        case 0x80: // Note Off
        case 0x90: // Note On
        case 0xA0: // Polyphonic Aftertouch
        case 0xB0: // Control Change
        case 0xE0: // Pitch Bend
            return 2;
        case 0xC0: // Program Change
        case 0xD0: // Channel Aftertouch
            return 1;
        default:
            return 0;
    }
}

} // namespace engine::core
