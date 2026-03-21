#include "AudioFileWriterProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <algorithm>
#include <sndfile.h>

namespace audio {

AudioFileWriterProcessor::AudioFileWriterProcessor(int sample_rate)
    : sample_rate_(sample_rate)
{
    declare_port({"audio_in", PORT_AUDIO, PortDirection::IN,
                  false, "Audio input to record"});

    declare_parameter({"max_seconds", "Max Duration (s)", 0.0f, 86400.0f, 0.0f,
                       false, "Maximum recording length in seconds; 0 = unlimited"});
    declare_parameter({"max_file_mb", "Max File Size (MB)", 0.0f, 4096.0f, 0.0f,
                       false, "Maximum output file size in MB; 0 = unlimited"});
}

AudioFileWriterProcessor::~AudioFileWriterProcessor() {
    close_file();
}

bool AudioFileWriterProcessor::apply_parameter(const std::string& name, float value) {
    if (name == "max_seconds") { max_seconds_ = std::max(0.0f, value); return true; }
    if (name == "max_file_mb") { max_file_mb_ = std::max(0.0f, value); return true; }
    return false;
}

bool AudioFileWriterProcessor::apply_string_parameter(const std::string& name,
                                                       const std::string& value) {
    if (name == "path") {
        close_file();
        if (!value.empty()) open_file(value);
        return true;
    }
    return false;
}

void AudioFileWriterProcessor::open_file(const std::string& path) {
    SF_INFO info{};
    info.samplerate = sample_rate_;
    info.channels   = 1;
    info.format     = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

    sf_ = sf_open(path.c_str(), SFM_WRITE, &info);
    frames_written_ = 0;

    if (max_seconds_ > 0.0f)
        max_frames_ = static_cast<long>(max_seconds_ * static_cast<float>(sample_rate_));
    else if (max_file_mb_ > 0.0f)
        // float WAV: 4 bytes/frame/channel — 1 channel here
        max_frames_ = static_cast<long>(max_file_mb_ * 1024.0f * 1024.0f / 4.0f);
    else
        max_frames_ = 0;  // unlimited
}

void AudioFileWriterProcessor::close_file() {
    if (sf_) {
        sf_close(sf_);
        sf_ = nullptr;
    }
    frames_written_ = 0;
}

void AudioFileWriterProcessor::flush_to_disk() {
    if (sf_) sf_write_sync(sf_);
}

void AudioFileWriterProcessor::do_pull(std::span<float> output, const VoiceContext*) {
    // Capture inline audio (output contains audio from the preceding node).
    if (sf_ && !output.empty()) {
        const long available = static_cast<long>(output.size());
        long to_write = available;
        if (max_frames_ > 0) {
            const long remaining = max_frames_ - frames_written_;
            if (remaining <= 0) return;  // limit reached — stop recording
            to_write = std::min(to_write, remaining);
        }
        const long written = sf_writef_float(sf_, output.data(), to_write);
        frames_written_ += written;
    }
    // Output span unchanged — transparent passthrough.
}

} // namespace audio

namespace {
[[maybe_unused]] const bool kRegistered = audio::ModuleRegistry::instance().register_module(
    "AUDIO_FILE_WRITER",
    "WAV file recorder sink — captures inline audio and writes to disk in real time",
    "Set path via engine_set_tag_string_param(handle, tag, \"path\", \"/output.wav\"). "
    "Call engine_file_writer_flush(handle) to force an OS-level sync. "
    "The file is closed and flushed automatically on engine_destroy(). "
    "Use max_seconds or max_file_mb to cap recording length. "
    "Role: SINK.",
    [](int sr) { return std::make_unique<audio::AudioFileWriterProcessor>(sr); }
);
} // namespace
