/**
 * @file Voice.hpp
 * @brief Represents a single synthesizer voice with a flexible topology.
 */

#ifndef AUDIO_VOICE_HPP
#define AUDIO_VOICE_HPP

#include "Processor.hpp"
#include "AudioBuffer.hpp"
#include "ModulationMatrix.hpp"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <array>

namespace audio {

/**
 * @brief A single synth voice with a dynamic signal chain.
 */
class Voice : public Processor {
public:
    explicit Voice(int sample_rate);

    /**
     * @brief Trigger the voice with a specific frequency.
     */
    void note_on(double frequency);
    
    /**
     * @brief Release the voice.
     */
    void note_off();

    /**
     * @brief Check if any envelope is in the release stage.
     */
    bool is_releasing() const;
    
    /**
     * @brief Immediately silence the voice.
     */
    void kill();

    /**
     * @brief Check if the voice is still active (e.g., envelopes are still releasing).
     */
    bool is_active() const;
    
    /**
     * @brief Reset all internal processors.
     */
    void reset() override;

    /**
     * @brief Add a processor to the signal chain.
     * @param p The processor to add.
     * @param tag Optional unique tag for node discovery.
     */
    void add_processor(std::unique_ptr<Processor> p, std::string tag = "");

    /**
     * @brief Get a processor by its unique tag.
     */
    Processor* get_processor_by_tag(const std::string& tag);

    /**
     * @brief Register a mapping from a global parameter ID to a specific node and parameter.
     */
    void register_parameter(int param_id, const std::string& tag, int internal_param_id);

    /**
     * @brief Set a parameter value by ID.
     */
    void set_parameter(int param_id, float value) override;

    /**
     * @brief Set the stereo pan position.
     */
    void set_pan(float pan) { pan_ = pan; }
    
    /**
     * @brief Get the stereo pan position.
     */
    float pan() const { return pan_; }

    /**
     * @brief Borrow a scratch buffer for parallel processing.
     * @param index The index of the scratch buffer (0-3).
     */
    std::span<float> get_scratch_buffer(size_t index);

    ModulationMatrix& matrix() { return matrix_; }

protected:
    /**
     * @brief Pull audio data from the flexible signal chain.
     */
    void do_pull(std::span<float> output, const VoiceContext* context = nullptr) override;

private:
    static constexpr size_t MAX_BLOCK_SIZE = 1024;
    static constexpr size_t SCRATCH_BUFFER_COUNT = 4;

    std::vector<std::unique_ptr<Processor>> signal_chain_;
    std::unordered_map<std::string, Processor*> tag_map_;
    
    struct ParameterRoute {
        std::string tag;
        int internal_id;
    };
    std::unordered_map<int, ParameterRoute> parameter_map_;

    std::array<AudioBuffer, SCRATCH_BUFFER_COUNT> scratch_buffers_;
    
    ModulationMatrix matrix_;
    
    int sample_rate_;
    float pan_; // -1.0 to 1.0

    // Temporary storage for active frequency
    double current_frequency_;
};

} // namespace audio

#endif // AUDIO_VOICE_HPP
