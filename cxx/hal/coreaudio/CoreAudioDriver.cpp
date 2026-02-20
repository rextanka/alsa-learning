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
    mono_buffer_.resize(static_cast<size_t>(block_size_));

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

OSStatus CoreAudioDriver::render_callback(
    void* inRefCon,
    AudioUnitRenderActionFlags* /* ioActionFlags */,
    const AudioTimeStamp* /* inTimeStamp */,
    UInt32 /* inBusNumber */,
    UInt32 inNumberFrames,
    AudioBufferList* ioData)
{
    auto* driver = static_cast<CoreAudioDriver*>(inRefCon);

    if (driver->callback_) {
        // CoreAudio usually requests the number of frames we initialized with,
        // but it can vary. Ensure our mono buffer is large enough.
        if (driver->mono_buffer_.size() < inNumberFrames) {
            driver->mono_buffer_.resize(inNumberFrames);
        }

        std::span<float> output_span(driver->mono_buffer_.data(), inNumberFrames);
        
        // Call the engine to fill the mono buffer
        driver->callback_(output_span);

        // Copy mono buffer to CoreAudio's stereo buffers (Non-Interleaved)
        // Bus 0: Left, Bus 1: Right (if configured as non-interleaved)
        // Actually, for kAudioFormatFlagIsNonInterleaved, ioData->mNumberBuffers will be 2.
        
        float* left = static_cast<float*>(ioData->mBuffers[0].mData);
        float* right = ioData->mNumberBuffers > 1 ? static_cast<float*>(ioData->mBuffers[1].mData) : nullptr;

        for (UInt32 i = 0; i < inNumberFrames; ++i) {
            float sample = output_span[i];
            left[i] = sample;
            if (right) {
                right[i] = sample;
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
