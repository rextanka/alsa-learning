#include "AudioFileReaderProcessor.hpp"
#include "../../core/ModuleRegistry.hpp"
#include <algorithm>
#include <cmath>

// libsndfile and libsamplerate — available via FetchContent
#include <sndfile.h>
#include <samplerate.h>

namespace audio {

AudioFileReaderProcessor::AudioFileReaderProcessor(int sample_rate)
    : engine_sr_(sample_rate)
{
    declare_port({"audio_out", PORT_AUDIO, PortDirection::OUT,
                  false, "File audio output (mono or stereo)"});

    declare_parameter({"loop", "Loop",      0.0f, 1.0f, 0.0f,
                       false, "1 = loop continuously; 0 = play once then silence"});
    declare_parameter({"gain", "Playback Gain", 0.0f, 4.0f, 1.0f,
                       false, "Output gain multiplier"});
}

void AudioFileReaderProcessor::reset() {
    read_pos_ = 0;
}

bool AudioFileReaderProcessor::apply_parameter(const std::string& name, float value) {
    if (name == "loop") { loop_ = (value >= 0.5f); return true; }
    if (name == "gain") { gain_ = std::clamp(value, 0.0f, 4.0f); return true; }
    return false;
}

bool AudioFileReaderProcessor::apply_string_parameter(const std::string& name,
                                                       const std::string& value) {
    if (name == "path") {
        load_file(value);
        return true;
    }
    return false;
}

void AudioFileReaderProcessor::load_file(const std::string& path) {
    samples_.clear();
    channels_  = 0;
    read_pos_  = 0;

    if (path.empty()) return;

    SF_INFO info{};
    SNDFILE* sf = sf_open(path.c_str(), SFM_READ, &info);
    if (!sf) return;

    if (info.channels < 1 || info.channels > 2) { sf_close(sf); return; }
    if (info.frames <= 0)                        { sf_close(sf); return; }

    // Read raw interleaved float samples
    std::vector<float> raw(static_cast<size_t>(info.frames * info.channels));
    sf_readf_float(sf, raw.data(), info.frames);
    sf_close(sf);

    // Resample if needed
    if (info.samplerate == engine_sr_) {
        samples_  = std::move(raw);
        channels_ = info.channels;
        return;
    }

    const double ratio = static_cast<double>(engine_sr_) / static_cast<double>(info.samplerate);
    const long out_frames = static_cast<long>(std::ceil(static_cast<double>(info.frames) * ratio));
    std::vector<float> resampled(static_cast<size_t>(out_frames * info.channels));

    SRC_DATA src_data{};
    src_data.data_in       = raw.data();
    src_data.input_frames  = info.frames;
    src_data.data_out      = resampled.data();
    src_data.output_frames = out_frames;
    src_data.src_ratio     = ratio;
    src_data.end_of_input  = 1;

    if (src_simple(&src_data, SRC_SINC_MEDIUM_QUALITY, info.channels) == 0) {
        // Trim to actual output frames
        resampled.resize(static_cast<size_t>(src_data.output_frames_gen * info.channels));
        samples_  = std::move(resampled);
        channels_ = info.channels;
    }
    // If SRC fails, leave samples_ empty (silence).
}

void AudioFileReaderProcessor::do_pull(std::span<float> output, const VoiceContext*) {
    const long total_frames = static_cast<long>(
        channels_ > 0 ? samples_.size() / static_cast<size_t>(channels_) : 0);

    for (size_t i = 0; i < output.size(); ++i) {
        if (total_frames == 0) { output[i] = 0.0f; continue; }
        if (read_pos_ >= total_frames) {
            if (loop_) read_pos_ = 0;
            else       { output[i] = 0.0f; continue; }
        }
        // Mono output: use left channel
        const float s = samples_[static_cast<size_t>(read_pos_ * channels_)] * gain_;
        output[i] = s;
        ++read_pos_;
    }
}

void AudioFileReaderProcessor::do_pull(AudioBuffer& output, const VoiceContext*) {
    const long total_frames = static_cast<long>(
        channels_ > 0 ? samples_.size() / static_cast<size_t>(channels_) : 0);
    const size_t n = output.left.size();

    for (size_t i = 0; i < n; ++i) {
        if (total_frames == 0) { output.left[i] = output.right[i] = 0.0f; continue; }
        if (read_pos_ >= total_frames) {
            if (loop_) read_pos_ = 0;
            else       { output.left[i] = output.right[i] = 0.0f; continue; }
        }
        const float l = samples_[static_cast<size_t>(read_pos_ * channels_)] * gain_;
        const float r = (channels_ == 2)
            ? samples_[static_cast<size_t>(read_pos_ * channels_ + 1)] * gain_
            : l;
        output.left[i]  = l;
        output.right[i] = r;
        ++read_pos_;
    }
}

} // namespace audio

namespace {
[[maybe_unused]] const bool kRegistered = audio::ModuleRegistry::instance().register_module(
    "AUDIO_FILE_READER",
    "WAV/AIFF file playback source — loads at path-set time, loops optionally",
    "Set path via engine_set_tag_string_param(handle, tag, \"path\", \"/file.wav\"). "
    "Supports WAV and AIFF (PCM 16/24/32, float 32/64). Mono or stereo only. "
    "SRC is applied if file sample rate differs from engine sample rate. "
    "Role: SOURCE.",
    [](int sr) { return std::make_unique<audio::AudioFileReaderProcessor>(sr); }
);
} // namespace
