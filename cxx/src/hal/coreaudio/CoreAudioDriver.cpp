/**
 * @file CoreAudioDriver.cpp
 * @brief Implementation of the CoreAudio driver for macOS.
 */

#include "CoreAudioDriver.hpp"
#include <iostream>

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
        std::cerr << "CoreAudioDriver: Failed to find default output component" << std::endl;
        return;
    }

    // 3. Open the audio unit
    OSStatus status = AudioComponentInstanceNew(comp, &audio_unit_);
    if (status != noErr) {
        std::cerr << "CoreAudioDriver: AudioComponentInstanceNew failed" << std::endl;
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
        std::cout << "CoreAudioDriver: Host sample rate detected as " << sample_rate_ << " Hz" << std::endl;
    }

    // 4. Set the stream format (PCM Float32, Stereo, Interleaved)
    AudioStreamBasicDescription streamFormat;
    streamFormat.mSampleRate = static_cast<double>(sample_rate_);
    streamFormat.mFormatID = kAudioFormatLinearPCM;
    streamFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
    streamFormat.mFramesPerPacket = 1;
    streamFormat.mChannelsPerFrame = 2; // Stereo
    streamFormat.mBitsPerChannel = 32;
    streamFormat.mBytesPerPacket = 4;
    streamFormat.mBytesPerFrame = 4;

    status = AudioUnitSetProperty(
        audio_unit_,
        kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input,
        0, // Output bus
        &streamFormat,
        sizeof(streamFormat));
    
    if (status != noErr) {
        std::cerr << "CoreAudioDriver: Failed to set stream format" << std::endl;
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
        std::cerr << "CoreAudioDriver: Failed to set render callback" << std::endl;
        return;
    }

    // 6. Initialize the audio unit
    status = AudioUnitInitialize(audio_unit_);
    if (status != noErr) {
        std::cerr << "CoreAudioDriver: AudioUnitInitialize failed" << std::endl;
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
        std::cout << "CoreAudioDriver: Final sample rate confirmed as " << sample_rate_ << " Hz" << std::endl;
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

} // namespace hal
