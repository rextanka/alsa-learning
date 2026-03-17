#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

namespace audio {

/**
 * @brief A single MIDI voice message extracted from an SMF file.
 *
 * Status bytes 0x80–0xEF only (Note On/Off, CC, Program Change, Pitch Bend,
 * Aftertouch). Meta-events and SysEx are consumed internally.
 *
 * Phase 22A: channel bits in `status` are preserved but ignored at dispatch.
 * Phase 22B: channel will be used to route events to per-channel VoiceGroups.
 */
struct SmfEvent {
    uint64_t abs_tick;  ///< Absolute position in file PPQ ticks from bar 0.
    uint8_t  status;    ///< MIDI status byte — channel encoded in low nibble.
    uint8_t  data1;     ///< First data byte (note number, controller, etc.).
    uint8_t  data2;     ///< Second data byte (velocity, value). 0 for 2-byte msgs.
};

/**
 * @brief A tempo change from an SMF FF 51 Set Tempo meta-event.
 */
struct TempoEvent {
    uint64_t abs_tick;    ///< Tick at which this tempo becomes active.
    uint32_t us_per_beat; ///< Microseconds per quarter note at this tempo.
};

/**
 * @brief Parsed representation of a Standard MIDI File.
 *
 * All tracks are merged into a single tick-sorted event list.
 * Ready for sample-accurate playback by MidiFilePlayer.
 */
struct MidiFileData {
    uint16_t ppq;                      ///< Ticks per quarter note (from SMF header).
    std::vector<TempoEvent> tempo_map; ///< Sorted by abs_tick; always has entry at tick 0.
    std::vector<SmfEvent>   events;    ///< All tracks merged and sorted by abs_tick.
};

/**
 * @brief Parses Standard MIDI Files (Format 0 and Format 1) into MidiFileData.
 *
 * Supported:
 *   - SMF Format 0 (single track) and Format 1 (multi-track simultaneous).
 *   - PPQ timing mode only (SMPTE rejected).
 *   - MIDI voice messages 0x80–0xEF.
 *   - Running status.
 *   - FF 51 Set Tempo — builds the tempo map.
 *   - FF 2F End of Track — terminates track parsing.
 *   - Note On with velocity 0 normalised to Note Off.
 *
 * Not supported:
 *   - SMF Format 2 (sequential multi-song) — throws.
 *   - SMPTE timing mode — throws.
 *   - SysEx (F0/F7) — skipped silently.
 */
class SmfParser {
public:
    /** Load and parse a .mid file. Throws std::runtime_error on any failure. */
    static MidiFileData load(const std::string& path);

private:
    static uint32_t read_vlq(const uint8_t* data, size_t size, size_t& pos);

    static void parse_track(const uint8_t* data, size_t size,
                            std::vector<SmfEvent>&   events_out,
                            std::vector<TempoEvent>& tempo_out);
};

} // namespace audio
