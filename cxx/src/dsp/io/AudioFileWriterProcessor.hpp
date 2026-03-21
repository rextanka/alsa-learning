#pragma once
#include "../Processor.hpp"
#include "AudioFileBackend.hpp"
#include <sndfile.h>
#include <string>
#include <memory>
#include <cstdint>

namespace audio {

/**
 * @brief AUDIO_FILE_WRITER — WAV file recorder sink (Phase 27C).
 *
 * Captures the inline audio stream and writes it to a WAV file in real time.
 * Flushes and closes the file on destruction or via flush_to_disk().
 *
 * Role: SINK (audio_in only — no audio_out).
 * Parameters: max_seconds (0 = unlimited), max_file_mb (0 = unlimited).
 * String parameters: path — set via engine_set_tag_string_param AFTER bake().
 *
 * do_pull() is a passthrough: inline audio from the preceding node is captured
 * and written to disk; the output span is left unchanged.
 */
class AudioFileWriterProcessor : public Processor {
public:
    explicit AudioFileWriterProcessor(int sample_rate = 48000);
    ~AudioFileWriterProcessor() override;

    void reset() override {}
    bool reset_on_note_on() const override { return false; }

    bool apply_parameter(const std::string& name, float value) override;
    bool apply_string_parameter(const std::string& name, const std::string& value) override;
    void flush_to_disk() override;

protected:
    void do_pull(std::span<float> output, const VoiceContext* ctx) override;

private:
    void open_file(const std::string& path);
    void close_file();

    int          sample_rate_;
    float        max_seconds_  = 0.0f;  ///< 0 = unlimited
    float        max_file_mb_  = 0.0f;  ///< 0 = unlimited

    SNDFILE*     sf_             = nullptr;
    long         frames_written_ = 0;
    long         max_frames_     = 0;  ///< derived from max_seconds_ at open
};

} // namespace audio
