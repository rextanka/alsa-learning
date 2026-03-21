/**
 * @file MidiFilePlayer.cpp
 * @brief Sample-accurate SMF sequencer — Phase 22A.
 */

#include "MidiFilePlayer.hpp"
#include "VoiceManager.hpp"
#include <cstdint>

namespace audio {

// ---------------------------------------------------------------------------
// Control — safe to call from any thread
// ---------------------------------------------------------------------------

void MidiFilePlayer::load(MidiFileData data) {
    playing_.store(false, std::memory_order_seq_cst);
    data_             = std::move(data);
    playhead_samples_ = 0.0;
    next_event_idx_   = 0;
}

void MidiFilePlayer::play() {
    playing_.store(true, std::memory_order_release);
}

void MidiFilePlayer::stop() {
    playing_.store(false, std::memory_order_release);
}

void MidiFilePlayer::rewind() {
    // Caller must stop() first to avoid a race on playhead state.
    playhead_samples_ = 0.0;
    next_event_idx_   = 0;
}

uint64_t MidiFilePlayer::position_ticks() const noexcept {
    if (data_.events.empty() || next_event_idx_ == 0) return 0;
    return data_.events[next_event_idx_ - 1].abs_tick;
}

// ---------------------------------------------------------------------------
// Timing conversion
// ---------------------------------------------------------------------------

double MidiFilePlayer::tick_to_sample(uint64_t tick,
                                       uint32_t sample_rate) const noexcept {
    if (data_.ppq == 0) return 0.0;

    double   total_samples = 0.0;
    uint32_t us_per_beat   = 500000; // default 120 BPM
    uint64_t last_tick     = 0;

    // Walk tempo segments up to (but not including) the target tick.
    for (const auto& te : data_.tempo_map) {
        if (te.abs_tick >= tick) break;

        // Accumulate samples for the segment [last_tick, te.abs_tick) using
        // the tempo that was active at last_tick.
        const uint64_t seg_ticks = te.abs_tick - last_tick;
        const double tick_dur_s  =
            static_cast<double>(us_per_beat) /
            (static_cast<double>(data_.ppq) * 1.0e6);
        total_samples += static_cast<double>(seg_ticks) * tick_dur_s *
                         static_cast<double>(sample_rate);

        last_tick    = te.abs_tick;
        us_per_beat  = te.us_per_beat;
    }

    // Final segment: [last_tick, tick) at current tempo.
    const uint64_t remaining = tick - last_tick;
    const double tick_dur_s  =
        static_cast<double>(us_per_beat) /
        (static_cast<double>(data_.ppq) * 1.0e6);
    total_samples += static_cast<double>(remaining) * tick_dur_s *
                     static_cast<double>(sample_rate);

    return total_samples;
}

// ---------------------------------------------------------------------------
// Audio-thread advance
// ---------------------------------------------------------------------------

void MidiFilePlayer::advance(uint32_t frames, uint32_t sample_rate,
                              VoiceManager& vm) {
    if (!playing_.load(std::memory_order_relaxed)) return;
    if (data_.events.empty() || data_.ppq == 0) {
        playing_.store(false, std::memory_order_release);  // nothing to play — auto-stop
        return;
    }

    const double block_end = playhead_samples_ + static_cast<double>(frames);

    while (next_event_idx_ < data_.events.size()) {
        const SmfEvent& ev = data_.events[next_event_idx_];
        const double event_sample = tick_to_sample(ev.abs_tick, sample_rate);

        if (event_sample >= block_end) break;

        // Sub-buffer sample offset: position within this block.
        uint32_t offset = 0;
        if (event_sample > playhead_samples_) {
            const double relative = event_sample - playhead_samples_;
            offset = static_cast<uint32_t>(relative);
            if (offset >= frames) offset = frames - 1;
        }

        // Dispatch via the existing bridge path.  The VoiceManager's MidiParser
        // handles note-to-pitch conversion via TuningSystem — no conversion here.
        // Phase 22A: channel bits in status are forwarded as-is but ignored for
        // routing (all voices share the single loaded patch).
        const uint8_t msg_type = ev.status & 0xF0;
        const uint8_t msg_len  = (msg_type == 0xC0 || msg_type == 0xD0) ? 2u : 3u;
        const uint8_t msg[3]   = { ev.status, ev.data1, ev.data2 };
        vm.processMidiBytes(msg, msg_len, offset);

        ++next_event_idx_;
    }

    playhead_samples_ = block_end;

    // Auto-stop once all events have been dispatched.
    if (next_event_idx_ >= data_.events.size()) {
        playing_.store(false, std::memory_order_release);
    }
}

} // namespace audio
