/**
 * @file SmfParser.cpp
 * @brief Standard MIDI File parser — Phase 22A.
 */

#include "SmfParser.hpp"
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <cstring>

namespace audio {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint32_t read_be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
}

static uint16_t read_be16(const uint8_t* p) {
    return (uint16_t(p[0]) << 8) | uint16_t(p[1]);
}

uint32_t SmfParser::read_vlq(const uint8_t* data, size_t size, size_t& pos) {
    uint32_t value = 0;
    for (int i = 0; i < 4 && pos < size; ++i) {
        uint8_t b = data[pos++];
        value = (value << 7) | (b & 0x7F);
        if (!(b & 0x80)) return value;
    }
    throw std::runtime_error("SmfParser: malformed variable-length quantity");
}

// ---------------------------------------------------------------------------
// Track parser
// ---------------------------------------------------------------------------

void SmfParser::parse_track(const uint8_t* data, size_t size,
                             std::vector<SmfEvent>&   events_out,
                             std::vector<TempoEvent>& tempo_out) {
    size_t   pos            = 0;
    uint64_t abs_tick       = 0;
    uint8_t  running_status = 0;

    while (pos < size) {
        uint32_t delta = read_vlq(data, size, pos);
        abs_tick += delta;

        if (pos >= size) break;
        uint8_t byte = data[pos];

        if (byte == 0xFF) {
            // ---- Meta-event ----
            ++pos;
            if (pos >= size) break;
            uint8_t meta_type = data[pos++];
            uint32_t meta_len = read_vlq(data, size, pos);
            if (pos + meta_len > size)
                throw std::runtime_error("SmfParser: meta-event overruns track");

            if (meta_type == 0x51 && meta_len == 3) {
                // Set Tempo: 3-byte µs/beat, big-endian
                uint32_t us = (uint32_t(data[pos])     << 16) |
                              (uint32_t(data[pos + 1]) <<  8) |
                               uint32_t(data[pos + 2]);
                tempo_out.push_back({abs_tick, us});
            } else if (meta_type == 0x2F) {
                // End of Track
                break;
            }
            pos += meta_len;
            running_status = 0; // meta-events cancel running status

        } else if (byte == 0xF0 || byte == 0xF7) {
            // ---- SysEx — skip ----
            ++pos;
            uint32_t sysex_len = read_vlq(data, size, pos);
            if (pos + sysex_len > size)
                throw std::runtime_error("SmfParser: SysEx overruns track");
            pos += sysex_len;
            running_status = 0;

        } else {
            // ---- Voice message ----
            uint8_t status;
            if (byte & 0x80) {
                status         = byte;
                running_status = byte;
                ++pos;
            } else {
                status = running_status; // running status — no status byte in stream
            }

            if (status == 0) break; // guard against corrupt data

            uint8_t msg_type = status & 0xF0;
            uint8_t d1 = 0, d2 = 0;

            if (msg_type == 0xC0 || msg_type == 0xD0) {
                // 2-byte messages: Program Change / Channel Pressure
                if (pos >= size) break;
                d1 = data[pos++];
            } else if (msg_type >= 0x80 && msg_type <= 0xEF) {
                // 3-byte messages: Note Off/On, Poly AT, CC, Pitch Bend
                if (pos + 1 > size) break;
                d1 = data[pos++];
                if (pos >= size) break;
                d2 = data[pos++];
            } else {
                break; // unknown status — stop track
            }

            // Normalise Note On velocity=0 to Note Off
            if (msg_type == 0x90 && d2 == 0) {
                status = (status & 0x0F) | 0x80;
            }

            events_out.push_back({abs_tick, status, d1, d2});
        }
    }
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

MidiFileData SmfParser::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("SmfParser: cannot open: " + path);

    f.seekg(0, std::ios::end);
    const size_t file_size = static_cast<size_t>(f.tellg());
    f.seekg(0);

    std::vector<uint8_t> buf(file_size);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(file_size));
    if (!f) throw std::runtime_error("SmfParser: read error: " + path);

    size_t pos = 0;

    // ---- Header chunk ----
    if (pos + 14 > file_size)
        throw std::runtime_error("SmfParser: file too small to be a valid SMF");
    if (std::memcmp(&buf[pos], "MThd", 4) != 0)
        throw std::runtime_error("SmfParser: missing MThd header");
    pos += 4;

    uint32_t header_len = read_be32(&buf[pos]); pos += 4;
    if (header_len < 6)
        throw std::runtime_error("SmfParser: invalid header length");

    uint16_t format   = read_be16(&buf[pos]); pos += 2;
    uint16_t n_tracks = read_be16(&buf[pos]); pos += 2;
    uint16_t division = read_be16(&buf[pos]); pos += 2;
    pos += header_len - 6; // skip any extra header bytes (forward-compat)

    if (format == 2)
        throw std::runtime_error("SmfParser: SMF Format 2 not supported");
    if (division & 0x8000)
        throw std::runtime_error("SmfParser: SMPTE timing mode not supported");

    MidiFileData result;
    result.ppq = division;

    std::vector<TempoEvent> all_tempos;
    std::vector<SmfEvent>   all_events;

    // ---- Track chunks ----
    for (uint16_t t = 0; t < n_tracks && pos < file_size; ++t) {
        if (pos + 8 > file_size) break;

        if (std::memcmp(&buf[pos], "MTrk", 4) != 0) {
            // Non-track chunk (e.g. SMPTE offset) — skip gracefully
            pos += 4;
            uint32_t chunk_len = read_be32(&buf[pos]); pos += 4;
            pos += chunk_len;
            continue;
        }
        pos += 4;
        uint32_t track_len = read_be32(&buf[pos]); pos += 4;
        if (pos + track_len > file_size)
            throw std::runtime_error("SmfParser: track data overruns file");

        std::vector<SmfEvent>   track_events;
        std::vector<TempoEvent> track_tempos;
        parse_track(&buf[pos], track_len, track_events, track_tempos);
        pos += track_len;

        for (auto& te : track_tempos) all_tempos.push_back(te);
        for (auto& ev : track_events) all_events.push_back(ev);
    }

    // Build tempo map: sort, then ensure tick-0 entry exists
    std::stable_sort(all_tempos.begin(), all_tempos.end(),
                     [](const TempoEvent& a, const TempoEvent& b) {
                         return a.abs_tick < b.abs_tick;
                     });

    // Remove duplicate tick positions — keep the last (file's explicit tempo wins)
    // Iterate in reverse and erase earlier duplicates.
    for (int i = static_cast<int>(all_tempos.size()) - 2; i >= 0; --i) {
        if (all_tempos[static_cast<size_t>(i)].abs_tick ==
            all_tempos[static_cast<size_t>(i + 1)].abs_tick) {
            all_tempos.erase(all_tempos.begin() + i);
        }
    }

    // Guarantee a tick-0 entry (default 120 BPM = 500 000 µs/beat)
    if (all_tempos.empty() || all_tempos[0].abs_tick != 0) {
        all_tempos.insert(all_tempos.begin(), {0u, 500000u});
    }
    result.tempo_map = std::move(all_tempos);

    // Merge-sort all voice events by abs_tick (stable preserves track ordering
    // for simultaneous events — important for correct chord reconstruction).
    std::stable_sort(all_events.begin(), all_events.end(),
                     [](const SmfEvent& a, const SmfEvent& b) {
                         return a.abs_tick < b.abs_tick;
                     });
    result.events = std::move(all_events);

    return result;
}

} // namespace audio
