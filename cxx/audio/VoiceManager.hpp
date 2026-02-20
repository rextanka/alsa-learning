/**
 * @file VoiceManager.hpp
 * @brief Manages a pool of polyphonic voices.
 * 
 * This file follows the project rules:
 * - NVI Pattern: Inherits from audio::Processor.
 * - Sample-rate independence: Passed to constructor.
 */

#ifndef AUDIO_VOICE_MANAGER_HPP
#define AUDIO_VOICE_MANAGER_HPP

#include "Processor.hpp"
#include "Voice.hpp"
#include <vector>
#include <array>
#include <memory>

namespace audio {

/**
 * @brief Manages a pool of voices for polyphonic playback.
 */
class VoiceManager : public Processor {
public:
    static constexpr int MAX_VOICES = 16;

    /**
     * @brief Construct a new Voice Manager object.
     * 
     * @param sample_rate Sample rate in Hz.
     */
    explicit VoiceManager(int sample_rate);

    /**
     * @brief Trigger a note on.
     * 
     * @param note MIDI note number.
     * @param velocity Note velocity (0.0 to 1.0).
     */
    void note_on(int note, float velocity);

    /**
     * @brief Trigger a note off.
     * 
     * @param note MIDI note number.
     */
    void note_off(int note);

    /**
     * @brief Reset all voices.
     */
    void reset() override;

protected:
    /**
     * @brief Pull audio from all active voices and sum them.
     * 
     * @param output Output buffer to fill.
     * @param context Optional voice context.
     */
    void do_pull(std::span<float> output, const VoiceContext* context = nullptr) override;

private:
    /**
     * @brief Convert MIDI note to frequency.
     */
    double note_to_freq(int note) const;

    struct VoiceSlot {
        std::unique_ptr<Voice> voice;
        int current_note = -1;
        bool active = false;
    };

    std::array<VoiceSlot, MAX_VOICES> voices_;
    int sample_rate_;
};

} // namespace audio

#endif // AUDIO_VOICE_MANAGER_HPP
