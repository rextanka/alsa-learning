/**
 * @file CoreAudioDriver.cpp  
 * @brief Implementation of the CoreAudio driver for macOS.
 */

#include "CoreAudioDriver.hpp"
#include "Logger.hpp"

namespace hal {

CoreAudioDriver::CoreAudioDriver(int sample_rate, int block_size)
    : audio_unit_(nullptr)
    , sample_rate_(sample_rate)
    , block_size_(block_size)
    , running_(false)
{
    left_buffer_.resize(static_cast<size_t>(block_size_));
    right_buffer_.resize(static_cast<size_t>(block_size_));

    // 1. Describe the audio unit
    AudioComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    // 2. Find the component
    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if (comp == NULL) {
        audio::AudioLogger::instance().log_message("CoreAudio", "Failed to find default output component");
        return;
    }

    // 3. Open the audio unit
    OSStatus status = AudioComponentInstanceNew(comp, &audio_unit_);
    if (status != noErr) {
        audio::AudioLogger::instance().log_message("CoreAudio", "AudioComponentInstanceNew failed");
        return;
    }

    // Query hardware sample rate
    AudioStreamBasicDescription hwFormat;
    UInt32 propSize = sizeof(hwFormat);
    status = AudioUnitGetProperty(
        audio_unit_,
        kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Output,
        0,
        &hwFormat,
        &propSize);

    if (status == noErr) {
        sample_rate_ = static_cast<int>(hwFormat.mSampleRate);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Host SR detected: %d Hz", sample_rate_);
        audio::AudioLogger::instance().log_message("CoreAudio", buf);
    }

    // 4. Set the stream format (PCM Float32, Stereo, Non-Interleaved)
    // Use the requested sample rate, capped at 48 kHz (hardware safety ceiling).
    const int capped_sr = std::min(sample_rate_, 48000);
    AudioStreamBasicDescription streamFormat{};  // zero-init all fields
    streamFormat.mSampleRate = static_cast<double>(capped_sr);
    streamFormat.mFormatID = kAudioFormatLinearPCM;
    streamFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsNonInterleaved | kAudioFormatFlagIsPacked;
    streamFormat.mBitsPerChannel = 32;
    streamFormat.mChannelsPerFrame = 2;
    streamFormat.mFramesPerPacket = 1;
    streamFormat.mBytesPerFrame = 4;
    streamFormat.mBytesPerPacket = 4;

    status = AudioUnitSetProperty(
        audio_unit_,
        kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input,
        0, // Output bus
        &streamFormat,
        sizeof(streamFormat));
    
    if (status != noErr) {
        audio::AudioLogger::instance().log_message("CoreAudio", "Failed to set stream format");
        return;
    }

    // 5. Set the render callback
    AURenderCallbackStruct callbackStruct;
    callbackStruct.inputProc = CoreAudioDriver::render_callback;
    callbackStruct.inputProcRefCon = this;

    status = AudioUnitSetProperty(
        audio_unit_,
        kAudioUnitProperty_SetRenderCallback,
        kAudioUnitScope_Input,
        0,
        &callbackStruct,
        sizeof(callbackStruct));

    if (status != noErr) {
        audio::AudioLogger::instance().log_message("CoreAudio", "Failed to set render callback");
        return;
    }

    // 6. Initialize the audio unit
    status = AudioUnitInitialize(audio_unit_);
    if (status != noErr) {
        audio::AudioLogger::instance().log_message("CoreAudio", "AudioUnitInitialize failed");
        return;
    }

    // Confirm actual sample rate after initialization
    propSize = sizeof(streamFormat);
    status = AudioUnitGetProperty(
        audio_unit_,
        kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input,
        0,
        &streamFormat,
        &propSize);

    if (status == noErr) {
        sample_rate_ = static_cast<int>(streamFormat.mSampleRate);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Final SR confirmed: %d Hz", sample_rate_);
        audio::AudioLogger::instance().log_message("CoreAudio", buf);
    }
}

CoreAudioDriver::~CoreAudioDriver() {
    stop();
    if (audio_unit_) {
        AudioUnitUninitialize(audio_unit_);
        AudioComponentInstanceDispose(audio_unit_);
    }
}

bool CoreAudioDriver::start() {
    if (running_) return true;
    if (!audio_unit_) return false;

    OSStatus status = AudioOutputUnitStart(audio_unit_);
    if (status == noErr) {
        running_ = true;
        return true;
    }
    return false;
}

void CoreAudioDriver::stop() {
    if (!running_) return;
    if (audio_unit_) {
        AudioOutputUnitStop(audio_unit_);
    }
    running_ = false;
}

void CoreAudioDriver::set_callback(AudioCallback callback) {
    callback_ = callback;
}

void CoreAudioDriver::set_stereo_callback(StereoAudioCallback callback) {
    stereo_callback_ = callback;
}

OSStatus CoreAudioDriver::render_callback(
    void* inRefCon,
    AudioUnitRenderActionFlags* /* ioActionFlags */,
    const AudioTimeStamp* /* inTimeStamp */,
    UInt32 /* inBusNumber */,
    UInt32 inNumberFrames,
    AudioBufferList* ioData)
{
    auto* driver = static_cast<CoreAudioDriver*>(inRefCon);

    if (driver->stereo_callback_ || driver->callback_) {
        if (driver->left_buffer_.size() < inNumberFrames) {
            driver->left_buffer_.resize(inNumberFrames);
            driver->right_buffer_.resize(inNumberFrames);
        }

        audio::AudioBuffer buffer;
        buffer.left = std::span<float>(driver->left_buffer_.data(), inNumberFrames);
        buffer.right = std::span<float>(driver->right_buffer_.data(), inNumberFrames);
        
        if (driver->stereo_callback_) {
            driver->stereo_callback_(buffer);
        } else {
            driver->callback_(buffer.left);
            std::copy(buffer.left.begin(), buffer.left.end(), buffer.right.begin());
        }

        float* out_l = static_cast<float*>(ioData->mBuffers[0].mData);
        float* out_r = ioData->mNumberBuffers > 1 ? static_cast<float*>(ioData->mBuffers[1].mData) : nullptr;

        for (UInt32 i = 0; i < inNumberFrames; ++i) {
            out_l[i] = buffer.left[i];
            if (out_r) {
                out_r[i] = buffer.right[i];
            }
        }
    } else {
        // Silence
        for (UInt32 b = 0; b < ioData->mNumberBuffers; ++b) {
            memset(ioData->mBuffers[b].mData, 0, ioData->mBuffers[b].mDataByteSize);
        }
    }

    return noErr;
}

// ---------------------------------------------------------------------------
// CoreAudioDriver::device_name — name of the currently open default device
// ---------------------------------------------------------------------------

std::string CoreAudioDriver::device_name() const {
    // Ask the HAL for the default output device, then query its name.
    AudioObjectPropertyAddress defaultAddr{
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioDeviceID deviceId = kAudioObjectUnknown;
    UInt32 size = sizeof(deviceId);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultAddr,
                                   0, nullptr, &size, &deviceId) != noErr
        || deviceId == kAudioObjectUnknown) {
        return "Default Output Device";
    }

    AudioObjectPropertyAddress nameAddr{
        kAudioDevicePropertyDeviceNameCFString,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    CFStringRef cfName = nullptr;
    size = sizeof(CFStringRef);
    if (AudioObjectGetPropertyData(deviceId, &nameAddr, 0, nullptr, &size, &cfName) != noErr
        || !cfName) {
        return "Default Output Device";
    }
    char buf[256]{};
    CFStringGetCString(cfName, buf, sizeof(buf), kCFStringEncodingUTF8);
    CFRelease(cfName);
    return buf;
}

// ---------------------------------------------------------------------------
// AudioDriver::enumerate_devices — CoreAudio implementation
// ---------------------------------------------------------------------------

static std::string ca_device_name(AudioDeviceID deviceId) {
    AudioObjectPropertyAddress addr{
        kAudioDevicePropertyDeviceNameCFString,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    CFStringRef cfName = nullptr;
    UInt32 size = sizeof(CFStringRef);
    if (AudioObjectGetPropertyData(deviceId, &addr, 0, nullptr, &size, &cfName) != noErr
        || !cfName) {
        return "Unknown Device";
    }
    char buf[256]{};
    CFStringGetCString(cfName, buf, sizeof(buf), kCFStringEncodingUTF8);
    CFRelease(cfName);
    return buf;
}

static int ca_default_sample_rate(AudioDeviceID deviceId) {
    AudioObjectPropertyAddress addr{
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    Float64 rate = 44100.0;
    UInt32 size = sizeof(Float64);
    AudioObjectGetPropertyData(deviceId, &addr, 0, nullptr, &size, &rate);
    return static_cast<int>(rate);
}

static std::vector<int> ca_supported_rates(AudioDeviceID deviceId) {
    AudioObjectPropertyAddress addr{
        kAudioDevicePropertyAvailableNominalSampleRates,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 dataSize = 0;
    if (AudioObjectGetPropertyDataSize(deviceId, &addr, 0, nullptr, &dataSize) != noErr)
        return {44100, 48000};

    std::vector<AudioValueRange> ranges(dataSize / sizeof(AudioValueRange));
    AudioObjectGetPropertyData(deviceId, &addr, 0, nullptr, &dataSize, ranges.data());

    static constexpr int kCommon[] = {22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000};
    std::vector<int> out;
    for (int rate : kCommon) {
        for (const auto& r : ranges) {
            if (rate >= static_cast<int>(r.mMinimum) && rate <= static_cast<int>(r.mMaximum)) {
                out.push_back(rate);
                break;
            }
        }
    }
    return out.empty() ? std::vector<int>{44100, 48000} : out;
}

static int ca_default_block_size(AudioDeviceID deviceId) {
    AudioObjectPropertyAddress addr{
        kAudioDevicePropertyBufferFrameSize,
        kAudioObjectPropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    UInt32 frames = 512;
    UInt32 size   = sizeof(UInt32);
    AudioObjectGetPropertyData(deviceId, &addr, 0, nullptr, &size, &frames);
    return static_cast<int>(frames);
}

static std::vector<int> ca_supported_block_sizes(AudioDeviceID deviceId) {
    AudioObjectPropertyAddress addr{
        kAudioDevicePropertyBufferFrameSizeRange,
        kAudioObjectPropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    AudioValueRange range{32.0, 4096.0};
    UInt32 size = sizeof(AudioValueRange);
    AudioObjectGetPropertyData(deviceId, &addr, 0, nullptr, &size, &range);

    std::vector<int> out;
    for (int s = 32; s <= 8192; s *= 2) {
        if (s >= static_cast<int>(range.mMinimum) && s <= static_cast<int>(range.mMaximum))
            out.push_back(s);
    }
    return out.empty() ? std::vector<int>{128, 256, 512, 1024} : out;
}

std::vector<HostDeviceInfo> AudioDriver::enumerate_devices() {
    // Retrieve all HAL audio device IDs
    AudioObjectPropertyAddress listAddr{
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 dataSize = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &listAddr,
                                       0, nullptr, &dataSize) != noErr)
        return {};

    const int count = static_cast<int>(dataSize / sizeof(AudioDeviceID));
    std::vector<AudioDeviceID> ids(count);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &listAddr,
                                   0, nullptr, &dataSize, ids.data()) != noErr)
        return {};

    std::vector<HostDeviceInfo> devices;
    int idx = 0;
    for (AudioDeviceID deviceId : ids) {
        // Filter to output-capable devices
        AudioObjectPropertyAddress outStreams{
            kAudioDevicePropertyStreams,
            kAudioObjectPropertyScopeOutput,
            kAudioObjectPropertyElementMain
        };
        UInt32 streamsSize = 0;
        AudioObjectGetPropertyDataSize(deviceId, &outStreams, 0, nullptr, &streamsSize);
        if (streamsSize == 0) continue;  // input-only

        HostDeviceInfo info;
        info.index                 = idx++;
        info.name                  = ca_device_name(deviceId);
        info.default_sample_rate   = ca_default_sample_rate(deviceId);
        info.default_block_size    = ca_default_block_size(deviceId);
        info.supported_sample_rates = ca_supported_rates(deviceId);
        info.supported_block_sizes  = ca_supported_block_sizes(deviceId);
        devices.push_back(std::move(info));
    }
    return devices;
}

} // namespace hal
