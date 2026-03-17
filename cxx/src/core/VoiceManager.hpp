/**
 * @file VoiceManager.hpp 
 * @brief Manages a pool of polyphonic voices.
 */

#ifndef AUDIO_VOICE_MANAGER_HPP
#define AUDIO_VOICE_MANAGER_HPP

#include "Processor.hpp"
#include "Voice.hpp"
#include "SummingBus.hpp"
#include "MidiEvent.hpp"
#include "MidiParser.hpp"
#include <vector>
#include <array>
#include <memory>
#include <functional>

namespace audio {

using namespace engine::core;

/**
 * @brief Manages a pool of voices for polyphonic playback.
 */
class AudioTap;

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
     * @brief Trigger a note on (group 0).
     *
     * @param note MIDI note number.
     * @param velocity Note velocity (0.0 to 1.0).
     * @param frequency Optional frequency (if <= 0, calculated from MIDI note).
     */
    void note_on(int note, float velocity, double frequency = 0.0);

    /**
     * @brief Trigger a note on in a specific voice group.
     *
     * Voice stealing considers only voices belonging to @p group_id.
     */
    void note_on(int note, float velocity, int group_id, double frequency);

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
     * @brief Set a parameter by name across all voices.
     */
    void set_parameter_by_name(const std::string& name, float value);

    /**
     * @brief Set a parameter by ID across all voices (all groups).
     */
    void set_parameter(int param_id, float value);

    /**
     * @brief Set a parameter by ID across all voices in a specific group.
     */
    void set_group_parameter(int group_id, int param_id, float value);

    /**
     * @brief Set filter type across all voices (all groups).
     */
    void set_filter_type(int type);

    /**
     * @brief Set filter type for all voices in a specific group.
     */
    void set_group_filter_type(int group_id, int type);

    /**
     * @brief Assign a voice slot to a voice group.
     *
     * @param voice_idx Index into the voice pool [0, MAX_VOICES).
     * @param group_id  Group identifier (arbitrary non-negative integer).
     */
    void assign_group(int voice_idx, int group_id);

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
     * @brief Set the polyphonic spread amount (0.0 to 1.0).
     * 0.0 = All voices centered.
     * 1.0 = Voices fully panned across the stereo field.
     */
    void set_voice_spread(float spread);

    /**
     * @brief Rebuild all voice slots using a factory.
     *
     * Called from engine_bake() after a new chain is described via
     * engine_add_module() / engine_connect_ports().  Must be called from
     * the control thread before engine_start().  Not RT-safe.
     *
     * @param factory Callable returning a fully-baked unique_ptr<Voice>.
     */
    void rebuild_all_voices(const std::function<std::unique_ptr<Voice>()>& factory);

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

    /**
     * @brief Set the diagnostic tap to monitor mono output.
     */
    void set_diagnostic_tap(AudioTap* tap) { diagnostic_tap_ = tap; }

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

public:
    /**
     * @brief Pull audio from all active voices and sum them (Stereo), feeding an optional diagnostic tap.
     */
    void pull_with_tap(AudioBuffer& output, Processor* tap, const VoiceContext* context = nullptr);

protected:

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
        int group_id = 0;               // Voice group (0 = default)
    };

    std::array<VoiceSlot, MAX_VOICES> voices_;
    std::array<int, 128> note_to_voice_map_; // Maps MIDI pitch to voice index
    MidiParser midi_parser_;
    int sample_rate_;
    std::unique_ptr<SummingBus> summing_bus_;

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
    float voice_spread_ = 0.5f; // Default 50% spread
    AudioTap* diagnostic_tap_ = nullptr;

public:
    const std::array<VoiceSlot, MAX_VOICES>& get_voices() const { return voices_; }

    /**
     * @brief Apply a function to every voice in the pool (control thread only).
     *
     * Used by the LFO C API to configure per-voice LFO and modulation matrix
     * without exposing the full VoiceSlot internals.
     */
    void for_each_voice(const std::function<void(Voice&)>& fn) {
        for (auto& slot : voices_) { fn(*slot.voice); }
    }
};

} // namespace audio

#endif // AUDIO_VOICE_MANAGER_HPP
