/**
 * @file VoiceManager.hpp
 * @brief Manages a pool of polyphonic voices.
 */

#ifndef AUDIO_VOICE_MANAGER_HPP
#define AUDIO_VOICE_MANAGER_HPP

#include "Processor.hpp"
#include "Voice.hpp"
#include "MidiEvent.hpp"
#include "MidiParser.hpp"
#include <vector>
#include <array>
#include <memory>

namespace audio {

using namespace engine::core;

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
     * @param frequency Optional frequency (if <= 0, calculated from MIDI note).
     */
    void note_on(int note, float velocity, double frequency = 0.0);

    /**
     * @brief Trigger a note on with specific panning.
     */
    void note_on_panned(int note, float velocity, float pan);

    /**
     * @brief Set pan for a currently playing note.
     */
    void set_note_pan(int note, float pan);

    /**
     * @brief Trigger a note off.
     * 
     * @param note MIDI note number.
     */
    void note_off(int note);

    /**
     * @brief Handle a MIDI event.
     */
    void handleMidiEvent(const MidiEvent& event);

    /**
     * @brief Process raw MIDI bytes.
     */
    void processMidiBytes(const uint8_t* data, size_t size, uint32_t sampleOffset);

    /**
     * @brief Reset all voices.
     */
    void reset() override;

    /**
     * @brief Clear all modulation connections for a specific processor ID.
     */
    void clear_connections(int id);

    /**
     * @brief Add a modulation connection.
     */
    void add_connection(int source_id, int target_id, int param, float intensity);

    /**
     * @brief Set a modulation source processor.
     */
    void set_mod_source(int id, std::shared_ptr<Processor> processor);

protected:
    /**
     * @brief Pull audio from all active voices and sum them.
     * 
     * @param output Output buffer to fill.
     * @param context Optional voice context.
     */
    void do_pull(std::span<float> output, const VoiceContext* context = nullptr) override;

    /**
     * @brief Pull audio from all active voices and sum them (Stereo).
     */
    void do_pull(AudioBuffer& output, const VoiceContext* context = nullptr) override;

private:
    /**
     * @brief Convert MIDI note to frequency.
     */
    double note_to_freq(int note) const;

    uint64_t next_timestamp() { return ++timestamp_counter_; }

    uint64_t timestamp_counter_ = 0;

    struct VoiceSlot {
        std::unique_ptr<Voice> voice;
        int current_note = -1;
        bool active = false;
        uint64_t last_note_on_time = 0; // For LRU stealing
    };

    std::array<VoiceSlot, MAX_VOICES> voices_;
    std::array<int, 128> note_to_voice_map_; // Maps MIDI pitch to voice index
    MidiParser midi_parser_;
    int sample_rate_;

public:
    struct Connection {
        int source_id;
        int target_id;
        int param;
        float intensity;
    };

    const std::vector<Connection>& get_connections() const { return connections_; }
    const std::unordered_map<int, std::shared_ptr<Processor>>& get_mod_sources() const { return mod_sources_; }

private:
    std::vector<Connection> connections_;
    std::unordered_map<int, std::shared_ptr<Processor>> mod_sources_;
    std::vector<float> mod_buffer_;

public:
    const std::array<VoiceSlot, MAX_VOICES>& get_voices() const { return voices_; }
};

} // namespace audio

#endif // AUDIO_VOICE_MANAGER_HPP
