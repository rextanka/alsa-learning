#include "../TestHelper.hpp"
#include <iostream>
#include <vector> 
#include <cmath>
#include <algorithm>
#include <iomanip>

/**
 * @file processor_check.cpp
 * @brief Verifies Oscillator and Processor functionality via Bridge API with Fidelity Auditing.
 */

/**
 * @brief Performs a fidelity audit on a generated buffer.
 * Uses hysteresis-based zero-crossing counting and symmetry checks.
 */
static bool verify_fidelity(const float* buffer, size_t frames, float expected_freq, float sample_rate, const char* name) {
    float max_val = -1.0f;
    float min_val = 1.0f;
    int crossings = 0;
    bool is_positive = false;
    const float hysteresis = 0.01f;

    for (size_t i = 0; i < frames; ++i) {
        float s = buffer[i];
        if (s > max_val) max_val = s;
        if (s < min_val) min_val = s;

        // Positive-going crossing with hysteresis
        if (!is_positive && s > hysteresis) {
            crossings++;
            is_positive = true;
        } else if (is_positive && s < -hysteresis) {
            is_positive = false;
        }
    }

    // 1. Symmetry Check: Verify no significant DC offset
    float symmetry = std::abs(max_val + min_val);
    bool symmetry_ok = symmetry < 0.05f;

    // 2. Frequency Check: Calculate based on positive-going crossings
    float duration = static_cast<float>(frames) / sample_rate;
    float actual_freq = static_cast<float>(crossings) / duration;
    
    // Boundary Logic: expect crossings to be within +/- 1 of expected
    float expected_crossings = duration * expected_freq;
    float freq_error = std::abs(actual_freq - expected_freq) / expected_freq;
    bool freq_ok = freq_error < 0.05f; // 5% tolerance

    std::cout << "  [" << name << "] Audit: Peak=" << std::fixed << std::setprecision(2) << max_val 
              << ", SymmErr=" << symmetry << ", Freq=" << actual_freq << "Hz (" 
              << (freq_ok ? "OK" : "FAIL") << ")" << std::endl;

    if (!symmetry_ok) std::cout << "    [WARN] Significant DC offset detected: " << symmetry << std::endl;
    
    return symmetry_ok && freq_ok && (max_val > 0.5f);
}

static bool run_c_bridge_oscillator_test(int sample_rate) {
    std::cout << "\n=== Oscillator C Bridge Fidelity Test ===" << std::endl;
    
    OscillatorHandle handle = oscillator_create(OSC_SINE, sample_rate);
    if (!handle) {
        std::cout << "Failed to create oscillator via C API" << std::endl;
        return false;
    }
    
    float target_freq = 440.0f;
    oscillator_set_frequency(handle, target_freq);
    
    // Increase to 8192 samples (16 blocks) for better frequency resolution
    const size_t frames = 8192;
    std::vector<float> buffer(frames);
    if (oscillator_process(handle, buffer.data(), frames) != 0) {
        oscillator_destroy(handle);
        return false;
    }
    
    uint64_t last_ns = 0, max_ns = 0, total_blocks = 0;
    if (oscillator_get_metrics(handle, &last_ns, &max_ns, &total_blocks) == 0) {
        std::cout << "C API metrics: last=" << last_ns << " ns, blocks=" << total_blocks << std::endl;
    }
    
    bool ok = verify_fidelity(buffer.data(), frames, target_freq, (float)sample_rate, "Sine VCO");
    
    oscillator_destroy(handle);
    return ok;
}

static bool run_wavetable_shapes_test(int sample_rate) {
    std::cout << "\n=== Wavetable C API Fidelity Test (Shapes) ===" << std::endl;

    auto test_shape = [&](int type, const char* name) -> bool {
        OscillatorHandle h = oscillator_create(type, sample_rate);
        if (!h) return false;
        
        float target_freq = 440.0f;
        oscillator_set_frequency(h, target_freq);
        
        // Analyze 8192 samples
        std::vector<float> buf(8192);
        if (oscillator_process(h, buf.data(), buf.size()) != 0) {
            oscillator_destroy(h);
            return false;
        }
        
        bool ok = verify_fidelity(buf.data(), buf.size(), target_freq, (float)sample_rate, name);
        oscillator_destroy(h);
        return ok;
    };

    bool ok = true;
    ok = test_shape(OSC_WAVETABLE_SINE, "Wavetable Sine") && ok;
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
        "Validates fundamental DSP processors with zero-crossing fidelity checks.",
        "Bridge API -> Internal Processors",
        "Accurate frequency and peak levels for all generated waves.",
        sample_rate
    );

    bool ok = true;
    ok = run_c_bridge_oscillator_test(sample_rate) && ok;
    ok = run_wavetable_shapes_test(sample_rate) && ok;

    std::cout << "\n=== Final Result ===" << std::endl;
    std::cout << (ok ? "✓ All fidelity tests passed!" : "✗ Some tests failed.") << std::endl;
    
    return ok ? 0 : 1;
}
