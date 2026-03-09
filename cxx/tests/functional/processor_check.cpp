#include "../TestHelper.hpp"
#include <iostream>
#include <vector>
#include <cmath>

/**
 * @file processor_check.cpp
 * @brief Verifies Oscillator and Processor functionality via Bridge API.
 */

static bool run_c_bridge_oscillator_test(int sample_rate) {
    std::cout << "\n=== Oscillator C Bridge Test ===" << std::endl;
    
    OscillatorHandle handle = oscillator_create(OSC_SINE, sample_rate);
    if (!handle) {
        std::cout << "Failed to create oscillator via C API" << std::endl;
        return false;
    }
    
    oscillator_set_frequency(handle, 880.0);
    
    const size_t frames = 512;
    std::vector<float> buffer(frames);
    if (oscillator_process(handle, buffer.data(), frames) != 0) {
        oscillator_destroy(handle);
        return false;
    }
    
    uint64_t last_ns = 0, max_ns = 0, total_blocks = 0;
    if (oscillator_get_metrics(handle, &last_ns, &max_ns, &total_blocks) == 0) {
        std::cout << "C API metrics: last=" << last_ns << " ns, blocks=" << total_blocks << std::endl;
    }
    
    float peak = 0.0f;
    for (float s : buffer) {
        float a = std::fabs(s);
        if (a > peak) peak = a;
    }
    std::cout << "Peak amplitude: " << peak << " " << (peak > 0.5f ? "OK" : "FAIL") << std::endl;
    
    oscillator_destroy(handle);
    return peak > 0.5f;
}

static bool run_wavetable_c_api_test(int sample_rate) {
    std::cout << "\n=== Wavetable C API Test (Shapes) ===" << std::endl;

    auto test_shape = [&](int type, const char* name) -> bool {
        OscillatorHandle h = oscillator_create(type, sample_rate);
        if (!h) return false;
        oscillator_set_frequency(h, 440.0);
        std::vector<float> buf(512);
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

    bool ok = test_shape(OSC_WAVETABLE_SINE, "Wavetable Sine");
    ok = test_shape(OSC_WAVETABLE_SAW, "Wavetable Saw") && ok;
    ok = test_shape(OSC_WAVETABLE_SQUARE, "Wavetable Square") && ok;
    ok = test_shape(OSC_WAVETABLE_TRIANGLE, "Wavetable Triangle") && ok;
    return ok;
}

int main() {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Processor & Bridge Audit",
        "Validates fundamental DSP processors and Bridge API handles.",
        "Bridge API -> Internal Processors",
        "Peaks > 0.5 for all generated waves; metrics reported.",
        sample_rate
    );

    bool ok = true;
    ok = run_c_bridge_oscillator_test(sample_rate) && ok;
    ok = run_wavetable_c_api_test(sample_rate) && ok;

    std::cout << "\n=== Final Result ===" << std::endl;
    std::cout << (ok ? "✓ All tests passed!" : "✗ Some tests failed.") << std::endl;
    
    return ok ? 0 : 1;
}
