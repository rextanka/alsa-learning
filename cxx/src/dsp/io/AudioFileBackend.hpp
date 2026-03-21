#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace audio {

/**
 * @brief Abstract file I/O backend for AUDIO_FILE_READER / AUDIO_FILE_WRITER (Phase 27C).
 *
 * The default implementation uses libsndfile + libsamplerate (LibSndfileBackend).
 * Only WAV and AIFF are supported — no compressed formats.
 */
struct AudioFileInfo {
    int    channels    = 0;
    int    sample_rate = 0;
    long   frames      = 0;   ///< total frames in file (0 for writers until closed)
};

class AudioFileReadBackend {
public:
    virtual ~AudioFileReadBackend() = default;

    /** Open file for reading. Returns true on success. */
    virtual bool open(const std::string& path) = 0;
    virtual void close() = 0;

    virtual AudioFileInfo info() const = 0;

    /**
     * Read @p frame_count interleaved frames into @p buf (size = frame_count * channels).
     * Returns actual frames read. 0 = EOF.
     */
    virtual long read_frames(float* buf, long frame_count) = 0;

    virtual bool is_open() const = 0;
};

class AudioFileWriteBackend {
public:
    virtual ~AudioFileWriteBackend() = default;

    /** Open file for writing. Returns true on success. */
    virtual bool open(const std::string& path, int sample_rate, int channels) = 0;
    virtual void close() = 0;

    /** Write @p frame_count interleaved frames from @p buf. Returns frames written. */
    virtual long write_frames(const float* buf, long frame_count) = 0;

    virtual void flush() = 0;
    virtual bool is_open() const = 0;
};

} // namespace audio
