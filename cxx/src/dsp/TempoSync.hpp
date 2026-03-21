/**
 * @file TempoSync.hpp
 * @brief Beat-relative division vocabulary and tempo-sync helpers (Phase 27D).
 *
 * All synced effects (ECHO_DELAY, LFO, PHASER) share this division table.
 * Divisions are beat-relative — they work correctly in any time signature.
 *
 * Usage:
 *   float secs = audio::beat_time_seconds(ctx->get_bpm(), division_index_);
 *   // Use secs as the target delay time / LFO period.
 */

#pragma once

#include <string_view>
#include <algorithm>

namespace audio {

/// Number of available beat divisions.
inline constexpr int kDivisionCount = 11;

/// Beat-relative multipliers (× one beat duration).
/// Index 0 = "whole" (4 beats), index 2 = "quarter" (1 beat), etc.
inline constexpr float kDivisionMultipliers[kDivisionCount] = {
    4.0f,          // 0: whole           — 2000 ms @ 120 BPM
    2.0f,          // 1: half            — 1000 ms @ 120 BPM
    1.0f,          // 2: quarter         —  500 ms @ 120 BPM (default)
    1.5f,          // 3: dotted_quarter  —  750 ms @ 120 BPM
    0.5f,          // 4: eighth          —  250 ms @ 120 BPM
    0.75f,         // 5: dotted_eighth   —  375 ms @ 120 BPM (classic Edge delay)
    2.0f / 3.0f,   // 6: triplet_quarter —  333 ms @ 120 BPM
    0.25f,         // 7: sixteenth       —  125 ms @ 120 BPM
    1.0f / 3.0f,   // 8: triplet_eighth  —  167 ms @ 120 BPM
    0.125f,        // 9: thirtysecond    —   62.5 ms @ 120 BPM
    0.0625f,       // 10: sixtyfourth   —   31.25 ms @ 120 BPM
};

/// Human-readable names matching the index positions above.
inline constexpr std::string_view kDivisionNames[kDivisionCount] = {
    "whole", "half", "quarter", "dotted_quarter",
    "eighth", "dotted_eighth", "triplet_quarter",
    "sixteenth", "triplet_eighth", "thirtysecond", "sixtyfourth"
};

/// Return the beat multiplier for a division index (clamped to valid range).
inline float division_multiplier(int index) {
    index = std::clamp(index, 0, kDivisionCount - 1);
    return kDivisionMultipliers[index];
}

/// Compute the time in seconds for one cycle at the given BPM and division index.
/// delay_time_seconds = (60 / bpm) × multiplier
inline float beat_time_seconds(float bpm, int division_index) {
    if (bpm <= 0.0f) bpm = 120.0f;
    return (60.0f / bpm) * division_multiplier(division_index);
}

/// Parse a division name string to its index. Returns -1 if not found.
inline int division_index_from_name(std::string_view name) {
    for (int i = 0; i < kDivisionCount; ++i)
        if (kDivisionNames[i] == name) return i;
    return -1;
}

} // namespace audio
