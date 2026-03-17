#pragma once

#include "SmfParser.hpp"
#include <atomic>
#include <cstdint>

namespace audio {

class VoiceManager;

/**
 * @brief Sample-accurate SMF sequencer — Phase 22A (channel-blind polyphonic).
 *
 * All Note On/Off events from all tracks are dispatched to a single VoiceManager
 * regardless of MIDI channel.  Channel-to-VoiceGroup routing is deferred to
 * Phase 22B (multi-timbral support).
 *
 * The existing midi_note_to_hz path in VoiceManager/TuningSystem handles pitch
 * conversion from MIDI note numbers — no conversion is needed here.
 *
 * ### Thread-safety
 *   advance() must be called from the audio thread only.
 *   load(), play(), stop(), rewind() may be called from any thread.
 *   Do not call rewind() while playing — call stop() first.
 *
 * ### Usage
 * @code
 *   player.load(SmfParser::load("piece.mid"));
 *   player.play();
 *   // each audio block:
 *   player.advance(frames, sample_rate, *voice_manager);
 * @endcode
 */
class MidiFilePlayer {
public:
    MidiFilePlayer() = default;

    /** Load parsed SMF data and reset the playhead to the start. */
    void load(MidiFileData data);

    /** Arm playback from the current playhead position. */
    void play();

    /** Suspend playback. Playhead stays at its current position. */
    void stop();

    /**
     * Reset playhead to the start of the file.
     * Call stop() first if currently playing.
     */
    void rewind();

    bool is_loaded()  const noexcept { return !data_.events.empty(); }
    bool is_playing() const noexcept { return playing_.load(std::memory_order_relaxed); }

    /**
     * Approximate playhead position in SMF ticks.
     * Authoritative only on the audio thread; approximate from other threads.
     */
    uint64_t position_ticks() const noexcept;

    /**
     * @brief Advance by @p frames samples and dispatch any events in the window.
     *
     * Called once per audio block (HAL callback or engine_process).
     * Events whose sample position falls in [playhead, playhead + frames) are
     * sent via vm.processMidiBytes() with the correct sub-buffer sampleOffset.
     *
     * Stops automatically when the last event has been dispatched.
     *
     * @param frames       Samples in this block.
     * @param sample_rate  Hardware sample rate (Hz).
     * @param vm           Destination VoiceManager.
     */
    void advance(uint32_t frames, uint32_t sample_rate, VoiceManager& vm);

private:
    /**
     * Convert an absolute SMF tick to a fractional sample offset from the
     * start of the file using the file's own tempo map.
     */
    double tick_to_sample(uint64_t tick, uint32_t sample_rate) const noexcept;

    MidiFileData      data_;
    std::atomic<bool> playing_{false};
    double            playhead_samples_{0.0}; ///< Audio-thread only.
    size_t            next_event_idx_{0};     ///< Audio-thread only.
};

} // namespace audio
