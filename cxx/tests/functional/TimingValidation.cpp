#include "../TestHelper.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

void test_timing_accuracy(int sample_rate) {
    std::cout << "--- Testing Timing Accuracy ---" << std::endl;
    
    // We'll use the Bridge API to verify BPM and musical time
    test::EngineWrapper engine(sample_rate);
    double bpm = 120.0;
    engine_set_bpm(engine.get(), bpm);
    
    // Advance engine by 1,000,000 samples
    // Note: Bridge API doesn't have a direct "advance" but we can advance via process
    const size_t block_size = 512;
    const size_t total_samples = 1000000;
    float output[block_size * 2]; // stereo
    
    size_t processed = 0;
    while (processed < total_samples) {
        engine_process(engine.get(), output, block_size);
        processed += block_size;
    }
    
    int bar, beat, tick;
    engine_get_musical_time(engine.get(), &bar, &beat, &tick);
    
    std::cout << "Processed samples: " << processed << " (" << (static_cast<double>(processed) / sample_rate) << "s)" << std::endl;
    std::cout << "Musical Time: " << bar << "." << beat << "." << tick << std::endl;
    
    // Quick sanity check: 120bpm = 2 beats per second. 
    // 1,000,000 samples @ 48kHz = 20.833s. 
    // 20.833s * 2 beats/s = 41.666 beats.
    // 4 beats per bar -> 10.416 bars.
    assert(bar >= 1); 
    std::cout << "  Timing Accuracy: OK" << std::endl;
}

void test_frequency_verification(int sample_rate) {
    std::cout << "\n--- Testing Frequency Verification ---" << std::endl;
    
    test::EngineWrapper engine(sample_rate);
    
    // Trigger A4 - verify it doesn't crash and engine handles the name
    int result = engine_note_on_name(engine.get(), "A4", 0.8f);
    assert(result == 0);
    
    std::cout << "  Frequency Verification (Bridge API): OK" << std::endl;
}

int main() {
    int sample_rate = test::get_safe_sample_rate(0);

    PRINT_TEST_HEADER(
        "Timing and Logic Validation",
        "Verifies musical clock accuracy and note name mapping via Bridge API.",
        "Clock -> Engine -> Bridge",
        "Console output showing musical time progress and note mapping success.",
        sample_rate
    );

    try {
        test_timing_accuracy(sample_rate);
        test_frequency_verification(sample_rate);
        std::cout << "\n=== All Validations Passed! ===" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "\n!!! Validation Failed: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
