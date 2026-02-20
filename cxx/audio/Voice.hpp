/**
 * @file Voice.hpp
 * @brief Represents a single synthesizer voice, combining an oscillator and an envelope.
 * 
 * This file follows the project rules:
 * - NVI Pattern: Inherits from audio::Processor.
 * - Sample-rate independence: Passed to constructor.
 * - Separation of Concerns: Combines DSP components.
 */

#ifndef AUDIO_VOICE_HPP
#define AUDIO_VOICE_HPP

#include "Processor.hpp"
#include "oscillator/WavetableOscillatorProcessor.hpp"
#include "envelope/AdsrEnvelopeProcessor.hpp"
#include <memory>

namespace audio {

/**
 * @brief A single synth voice.
 * 
 * Each voice consists of an oscillator and an envelope.
 */
class Voice : public Processor {
public:
    /**
     * @brief Construct a new Voice object.
     * 
     * @param sample_rate Sample rate in Hz.
     */
    explicit Voice(int sample_rate);

    /**
     * @brief Trigger note on (gate on).
     * 
     * @param frequency Frequency in Hz.
     */
    void note_on(double frequency);

    /**
     * @brief Trigger note off (gate off).
     */
    void note_off();

    /**
     * @brief Check if the voice is currently active (envelope is not idle).
     * 
     * @return true if active, false otherwise.
     */
    bool is_active() const;

    /**
     * @brief Reset the voice state.
     */
    void reset() override;

    /**
     * @brief Access the oscillator processor.
     */
    WavetableOscillatorProcessor& oscillator() { return *oscillator_; }

    /**
     * @brief Access the envelope processor.
     */
    AdsrEnvelopeProcessor& envelope() { return *envelope_; }

protected:
    /**
     * @brief Implementation of the pull-based processing logic.
     * 
     * @param output Output buffer to fill.
     * @param context Optional voice context.
     */
    void do_pull(std::span<float> output, const VoiceContext* context = nullptr) override;

private:
    std::unique_ptr<WavetableOscillatorProcessor> oscillator_;
    std::unique_ptr<AdsrEnvelopeProcessor> envelope_;
    int sample_rate_;
};

} // namespace audio

#endif // AUDIO_VOICE_HPP
