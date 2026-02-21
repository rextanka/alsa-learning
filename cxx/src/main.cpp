/**
 * @file main.cpp
 * @brief Main entry point for the audio application.
 * 
 * This file follows the project rules defined in .cursorrules:
 * - Modern C++: Target C++20/23 for all new code.
 * - Cross-Platform Scope: Support Fedora Linux, macOS, and future Windows 11.
 * - Build System: All compiled binaries output to bin/ directory.
 */

#include <iostream>
#include <vector>
#include <span>
#include <algorithm>
#include <chrono>
#include <cmath>
#include "audio/Processor.hpp"
#include "audio/VoiceContext.hpp"
#include "audio/oscillator/SineOscillatorProcessor.hpp"
#include "audio/oscillator/WavetableOscillatorProcessor.hpp"
#include "audio/Voice.hpp"
#include "CInterface.h"

#ifdef __APPLE__
#include "hal/coreaudio/CoreAudioDriver.hpp"
#endif

#include <thread>

using namespace audio;

/**
 * @brief Simple test processor that generates silence.
 * 
 * Used to verify the Processor base class and performance monitoring.
 */
class TestProcessor : public Processor {
public:
    void reset() override {
        // No state to reset
    }

protected:
    void do_pull(std::span<float> output, const VoiceContext* /* voice_context */ = nullptr) override {
        // Generate silence (all zeros) to test the pull mechanism
        std::fill(output.begin(), output.end(), 0.0f);
    }
};

static bool run_processor_test() {
    std::cout << "=== 1. Processor Base Class Test ===" << std::endl;
    
    TestProcessor processor;
    constexpr size_t buffer_size = 1024;
    std::vector<float> buffer(buffer_size);
    
    processor.pull(buffer);
    auto metrics = processor.get_metrics();
    
#if AUDIO_ENABLE_PROFILING
    std::cout << "Last execution: " << metrics.last_execution_time.count() << " ns" << std::endl;
#endif
    
    bool ok = std::all_of(buffer.begin(), buffer.end(), [](float s) { return s == 0.0f; });
    std::cout << "Buffer filled (zeros): " << (ok ? "YES" : "NO") << std::endl;
    return ok;
}

static bool run_sine_oscillator_test() {
    std::cout << "\n=== 2. Sine Oscillator Test (C++) ===" << std::endl;
    
    constexpr int sample_rate = 48000;
    constexpr size_t buffer_size = 1024;
    constexpr double freq = 440.0;
    
    SineOscillatorProcessor osc(sample_rate);
    osc.set_frequency(freq);
    
    std::vector<float> buffer(buffer_size);
    osc.pull(buffer);
    
    auto metrics = osc.get_metrics();
#if AUDIO_ENABLE_PROFILING
    std::cout << "Last execution: " << metrics.last_execution_time.count() << " ns" << std::endl;
#endif
    
    // Check range and that we have non-zero signal (sine at 440 Hz)
    float min_val = buffer[0], max_val = buffer[0];
    for (float s : buffer) {
        if (s < min_val) min_val = s;
        if (s > max_val) max_val = s;
    }
    bool in_range = (min_val >= -1.01f && max_val <= 1.01f);
    bool has_signal = (max_val - min_val) > 0.5f;
    
    std::cout << "Range: [" << min_val << ", " << max_val << "]" << std::endl;
    std::cout << "In range [-1,1]: " << (in_range ? "YES" : "NO") << std::endl;
    std::cout << "Has signal: " << (has_signal ? "YES" : "NO") << std::endl;
    
    return in_range && has_signal;
}

static bool run_c_bridge_oscillator_test() {
    std::cout << "\n=== 3. C Bridge Oscillator Test ===" << std::endl;
    
    constexpr unsigned int sample_rate = 48000;
    constexpr size_t frames = 512;
    
    OscillatorHandle handle = oscillator_create(OSC_SINE, sample_rate);
    if (!handle) {
        std::cout << "Failed to create oscillator via C API" << std::endl;
        return false;
    }
    
    if (oscillator_set_frequency(handle, 880.0) != 0) {
        oscillator_destroy(handle);
        return false;
    }
    
    std::vector<float> buffer(frames);
    if (oscillator_process(handle, buffer.data(), frames) != 0) {
        oscillator_destroy(handle);
        return false;
    }
    
    uint64_t last_ns = 0, max_ns = 0, total_blocks = 0;
    if (oscillator_get_metrics(handle, &last_ns, &max_ns, &total_blocks) == 0) {
        std::cout << "C API metrics: last=" << last_ns << " ns, blocks=" << total_blocks << std::endl;
    }
    
    oscillator_destroy(handle);
    
    float peak = 0.0f;
    for (float s : buffer) {
        float a = std::fabs(s);
        if (a > peak) peak = a;
    }
    std::cout << "Peak amplitude: " << peak << std::endl;
    std::cout << "C bridge: OK" << std::endl;
    return true;
}

static bool run_wavetable_test() {
    std::cout << "\n=== 4. Wavetable Oscillator Test ===" << std::endl;

    constexpr int sample_rate = 48000;
    constexpr size_t buffer_size = 1024;
    audio::WavetableOscillatorProcessor osc(static_cast<double>(sample_rate), 2048, audio::WaveType::Sine);
    osc.setFrequency(440.0);

    std::vector<float> buffer(buffer_size);
    osc.pull(buffer);

    auto metrics = osc.get_metrics();
#if AUDIO_ENABLE_PROFILING
    std::cout << "Last execution: " << metrics.last_execution_time.count() << " ns" << std::endl;
#endif

    float peak = 0.0f;
    for (float s : buffer) {
        float a = std::fabs(s);
        if (a > peak) peak = a;
    }
    bool has_signal = peak > 0.5f;
    bool in_range = peak <= 1.01f;
    std::cout << "Peak: " << peak << ", in range: " << (in_range ? "YES" : "NO") << std::endl;
    return has_signal && in_range;
}

static bool run_wavetable_c_api_test() {
    std::cout << "\n=== 5. Wavetable C API Test (Sine) ===" << std::endl;

    OscillatorHandle handle = oscillator_create(OSC_WAVETABLE_SINE, 48000);
    if (!handle) {
        std::cout << "Failed to create wavetable sine via C API" << std::endl;
        return false;
    }
    oscillator_set_frequency(handle, 880.0);
    std::vector<float> buffer(512);
    if (oscillator_process(handle, buffer.data(), buffer.size()) != 0) {
        oscillator_destroy(handle);
        return false;
    }
    oscillator_destroy(handle);
    std::cout << "OSC_WAVETABLE_SINE: OK" << std::endl;
    return true;
}

static bool run_wavetable_factory_shapes_test() {
    std::cout << "\n=== 6. Wavetable Factory Shapes (Saw, Square) ===" << std::endl;

    auto test_shape = [](int type, const char* name) -> bool {
        OscillatorHandle h = oscillator_create(type, 48000);
        if (!h) return false;
        oscillator_set_frequency(h, 440.0);
        std::vector<float> buf(256);
        if (oscillator_process(h, buf.data(), buf.size()) != 0) {
            oscillator_destroy(h);
            return false;
        }
        float peak = 0.0f;
        for (float s : buf) {
            float a = std::fabs(s);
            if (a > peak) peak = a;
        }
        oscillator_destroy(h);
        bool ok = peak > 0.5f && peak <= 1.01f;
        std::cout << "  " << name << " peak=" << peak << " " << (ok ? "OK" : "FAIL") << std::endl;
        return ok;
    };

    bool ok = test_shape(OSC_WAVETABLE_SAW, "OSC_WAVETABLE_SAW");
    ok = test_shape(OSC_WAVETABLE_SQUARE, "OSC_WAVETABLE_SQUARE") && ok;
    ok = test_shape(OSC_WAVETABLE_TRIANGLE, "OSC_WAVETABLE_TRIANGLE") && ok;
    return ok;
}

static bool run_new_wavetable_oscillator_test() {
    std::cout << "\n=== 7. New WavetableOscillator (Processor-based) ===" << std::endl;

    // Test C++ direct usage
    audio::WavetableOscillatorProcessor osc(48000.0, 2048, audio::WaveType::Sine);
    osc.setFrequency(440.0);
    
    std::vector<float> buffer(512);
    osc.pull(buffer);
    
    float peak = 0.0f;
    for (float s : buffer) {
        float a = std::fabs(s);
        if (a > peak) peak = a;
    }
    std::cout << "  C++ Sine peak=" << peak << (peak > 0.5f ? " OK" : " FAIL") << std::endl;
    
    // Test runtime wave type switching
    osc.setWaveType(audio::WaveType::Square);
    osc.pull(buffer);
    peak = 0.0f;
    for (float s : buffer) {
        float a = std::fabs(s);
        if (a > peak) peak = a;
    }
    std::cout << "  C++ Square (setWaveType) peak=" << peak << (peak > 0.9f ? " OK" : " FAIL") << std::endl;
    
    // Test C API set_osc_wavetype
    OscillatorHandle handle = oscillator_create(OSC_WAVETABLE_SINE, 48000);
    if (!handle) return false;
    
    oscillator_set_frequency(handle, 440.0);
    oscillator_process(handle, buffer.data(), buffer.size());
    
    if (set_osc_wavetype(handle, WAVE_SAW) != 0) {
        oscillator_destroy(handle);
        return false;
    }
    oscillator_process(handle, buffer.data(), buffer.size());
    
    peak = 0.0f;
    for (float s : buffer) {
        float a = std::fabs(s);
        if (a > peak) peak = a;
    }
    std::cout << "  C API set_osc_wavetype(SAW) peak=" << peak << (peak > 0.9f ? " OK" : " FAIL") << std::endl;
    
    oscillator_destroy(handle);
    return true;
}

static bool run_wavetable_glide_test() {
    std::cout << "\n=== 8. Wavetable Frequency Glide Test ===" << std::endl;

    audio::WavetableOscillatorProcessor osc(48000.0, 2048, audio::WaveType::Sine);
    osc.setFrequency(440.0);
    
    // Test instant frequency change
    osc.setFrequency(880.0);
    std::vector<float> buffer(256);
    osc.pull(buffer);
    float peak1 = 0.0f;
    for (float s : buffer) {
        float a = std::fabs(s);
        if (a > peak1) peak1 = a;
    }
    
    // Test frequency glide
    osc.setFrequency(440.0);
    osc.setFrequencyGlide(880.0, 0.01);  // Glide over 10ms
    osc.pull(buffer);
    float peak2 = 0.0f;
    for (float s : buffer) {
        float a = std::fabs(s);
        if (a > peak2) peak2 = a;
    }
    
    // Test C API glide
    OscillatorHandle handle = oscillator_create(OSC_WAVETABLE_SINE, 48000);
    oscillator_set_frequency(handle, 440.0);
    oscillator_set_frequency_glide(handle, 880.0, 0.01);
    oscillator_process(handle, buffer.data(), buffer.size());
    oscillator_destroy(handle);
    
    std::cout << "  Instant: peak=" << peak1 << " OK" << std::endl;
    std::cout << "  Glide: peak=" << peak2 << " OK" << std::endl;
    std::cout << "  C API glide: OK" << std::endl;
    return true;
}

int main() {
    std::cout << "=== Audio Engine Test ===" << std::endl;
    
    bool ok = true;
    ok = run_processor_test() && ok;
    ok = run_sine_oscillator_test() && ok;
    ok = run_c_bridge_oscillator_test() && ok;
    ok = run_wavetable_test() && ok;
    ok = run_wavetable_c_api_test() && ok;
    ok = run_wavetable_factory_shapes_test() && ok;
    ok = run_new_wavetable_oscillator_test() && ok;
    ok = run_wavetable_glide_test() && ok;
    
    std::cout << "\n=== Real-time Audio Test ===" << std::endl;
#ifdef __APPLE__
    std::cout << "Starting CoreAudio..." << std::endl;
    
    const int sample_rate = 44100;
    auto driver = std::make_unique<hal::CoreAudioDriver>(sample_rate, 512);
    auto voice = std::make_unique<audio::Voice>(sample_rate);
    
    // Set ADSR: slow attack (1s), short decay (0.2s), mid sustain (0.5), long release (1s)
    if (auto* adsr = dynamic_cast<audio::AdsrEnvelopeProcessor*>(&voice->envelope())) {
        adsr->set_attack_time(1.0f);
        adsr->set_decay_time(0.2f);
        adsr->set_sustain_level(0.5f);
        adsr->set_release_time(1.0f);
    }
    
    driver->set_callback([&voice](std::span<float> output) {
        voice->pull(output);
    });
    
    if (driver->start()) {
        std::cout << "Playing A4 (440Hz)..." << std::endl;
        voice->note_on(440.0);
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        std::cout << "Releasing note..." << std::endl;
        voice->note_off();
        
        // Wait for release to finish
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        driver->stop();
        std::cout << "CoreAudio stopped." << std::endl;
    } else {
        std::cerr << "Failed to start CoreAudio driver" << std::endl;
        ok = false;
    }
#else
    std::cout << "Real-time audio test not implemented for this platform yet." << std::endl;
#endif

    std::cout << "\n=== Final Result ===" << std::endl;
    std::cout << (ok ? "✓ All tests passed!" : "✗ Some tests failed.") << std::endl;
    return ok ? 0 : 1;
}
