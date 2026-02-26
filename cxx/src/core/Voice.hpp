/**
 * @file Voice.hpp
 * @brief Represents a single synthesizer voice.
 */

#ifndef AUDIO_VOICE_HPP
#define AUDIO_VOICE_HPP

#include "Processor.hpp"
#include "oscillator/OscillatorProcessor.hpp"
#include "envelope/AdsrEnvelopeProcessor.hpp"
#include "filter/FilterProcessor.hpp"
#include "oscillator/LfoProcessor.hpp"
#include "oscillator/SubOscillator.hpp"
#include "routing/SourceMixer.hpp"
#include "AudioGraph.hpp"
#include "ModulationMatrix.hpp"
#include <memory>
#include <array>

namespace audio {

/**
 * @brief A single synth voice.
 */
class Voice : public Processor {
public:
    explicit Voice(int sample_rate);

    void note_on(double frequency);
    void note_off();

    SourceMixer& source_mixer() { return *source_mixer_; }
    SubOscillator& sub_oscillator() { return *sub_oscillator_; }
    bool is_active() const;
    void reset() override;

    OscillatorProcessor& oscillator() { return *oscillator_; }
    EnvelopeProcessor& envelope() { return *envelope_; }
    FilterProcessor* filter() { return filter_.get(); }
    LfoProcessor& lfo() { return *lfo_; }
    ModulationMatrix& matrix() { return matrix_; }

    void set_filter_type(std::unique_ptr<FilterProcessor> filter);
    BufferPool::BufferPtr borrow_buffer() { return graph_->borrow_buffer(); }

    /**
     * @brief Set a modulation parameter.
     */
    void set_parameter(int param, float value);

    void set_pan(float pan);
    float pan() const { return pan_; }

    SourceMixer& source_mixer() { return *source_mixer_; }
    SubOscillator& sub_oscillator() { return *sub_oscillator_; }

    /**
     * @brief Internal parameter control for Voice components.
     */
    int set_internal_param(const std::string& name, float value);

protected:
    void do_pull(std::span<float> output, const VoiceContext* context = nullptr) override;
    void do_pull(AudioBuffer& output, const VoiceContext* context = nullptr) override;

    // Buffer for source mixing
    static constexpr size_t MAX_BLOCK_SIZE = 1024;

private:
    static constexpr size_t MAX_BLOCK_SIZE = 1024;

    void rebuild_graph();
    void apply_modulation();

    std::unique_ptr<OscillatorProcessor> oscillator_;
    std::unique_ptr<SubOscillator> sub_oscillator_;
    std::unique_ptr<SourceMixer> source_mixer_;
    std::unique_ptr<AdsrEnvelopeProcessor> envelope_;
    std::unique_ptr<FilterProcessor> filter_;
    std::unique_ptr<LfoProcessor> lfo_;
    std::unique_ptr<AudioGraph> graph_;
    
    std::unique_ptr<SubOscillator> sub_oscillator_;
    std::unique_ptr<SourceMixer> source_mixer_;

    ModulationMatrix matrix_;
    
    // Base parameters (anchors for modulation)
    double base_frequency_;
    float base_cutoff_;
    float base_resonance_;
    float base_amplitude_;

    int sample_rate_;
    float pan_; // -1.0 to 1.0

    // Temporary buffers for modulation sources
    std::array<float, static_cast<size_t>(ModulationSource::Count)> current_source_values_;
};

} // namespace audio

#endif // AUDIO_VOICE_HPP
