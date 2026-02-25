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
#include "AudioGraph.hpp"
#include <memory>

namespace audio {

/**
 * @brief A single synth voice.
 */
class Voice : public Processor {
public:
    explicit Voice(int sample_rate);

    void note_on(double frequency);
    void note_off();
    bool is_active() const;
    void reset() override;

    OscillatorProcessor& oscillator() { return *oscillator_; }
    EnvelopeProcessor& envelope() { return *envelope_; }
    FilterProcessor* filter() { return filter_.get(); }

    void set_filter_type(std::unique_ptr<FilterProcessor> filter);
    BufferPool::BufferPtr borrow_buffer() { return graph_->borrow_buffer(); }

    void set_pan(float pan);
    float pan() const { return pan_; }

protected:
    void do_pull(std::span<float> output, const VoiceContext* context = nullptr) override;
    void do_pull(AudioBuffer& output, const VoiceContext* context = nullptr) override;

private:
    void rebuild_graph();

    std::unique_ptr<OscillatorProcessor> oscillator_;
    std::unique_ptr<AdsrEnvelopeProcessor> envelope_;
    std::unique_ptr<FilterProcessor> filter_;
    std::unique_ptr<AudioGraph> graph_;
    int sample_rate_;
    float pan_; // -1.0 to 1.0
};

} // namespace audio

#endif // AUDIO_VOICE_HPP
