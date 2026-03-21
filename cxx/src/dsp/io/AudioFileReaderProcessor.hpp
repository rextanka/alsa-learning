#pragma once
#include "../Processor.hpp"
#include "AudioFileBackend.hpp"
#include <memory>
#include <string>
#include <vector>

namespace audio {

/**
 * @brief AUDIO_FILE_READER — WAV/AIFF playback source (Phase 27C).
 *
 * Reads a WAV or AIFF file into memory at construction (when path is set),
 * performs SRC to the engine sample rate, and outputs audio block by block.
 *
 * Role: SOURCE (audio_out only — mono or stereo).
 * Parameters: loop (0=off, 1=on), gain (0–4, default 1.0).
 * String parameters: path (file path — set via engine_set_tag_string_param).
 *
 * Mono files → mono output (left channel).
 * Stereo files → stereo output (left channel in mono pull; stereo in stereo pull).
 * Files with > 2 channels are not supported (open fails silently).
 */
class AudioFileReaderProcessor : public Processor {
public:
    explicit AudioFileReaderProcessor(int sample_rate = 48000);
    ~AudioFileReaderProcessor() override = default;

    void reset() override;
    bool reset_on_note_on() const override { return false; }

    bool apply_parameter(const std::string& name, float value) override;
    bool apply_string_parameter(const std::string& name, const std::string& value) override;

protected:
    void do_pull(std::span<float>  output, const VoiceContext* ctx) override;
    void do_pull(AudioBuffer& output, const VoiceContext* ctx) override;

private:
    void load_file(const std::string& path);

    int    engine_sr_;
    bool   loop_      = false;
    float  gain_      = 1.0f;
    int    channels_  = 0;   ///< 1 or 2; 0 = not loaded

    // Interleaved PCM data after SRC, at engine_sr_.
    std::vector<float> samples_;   ///< interleaved: [L0,R0,L1,R1,...] or [L0,L1,...]
    long               read_pos_  = 0;  ///< current read position in frames
};

} // namespace audio
