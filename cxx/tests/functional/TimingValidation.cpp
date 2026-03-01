#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <memory>
#include <functional>
#include "../../src/core/MusicalClock.hpp"
#include "../../src/core/TuningSystem.hpp"
#include "../../include/CInterface.h"

// For testing internal state
#include "../../src/core/VoiceManager.hpp"
#include "../../src/hal/AudioDriver.hpp"

enum class HandleType {
    Oscillator,
    Envelope,
    Engine,
    GenericProcessor
};

struct HandleBase {
    HandleType type;
    explicit HandleBase(HandleType t) : type(t) {}
    virtual ~HandleBase() = default;
};

struct InternalEngine : public HandleBase {
    std::unique_ptr<audio::VoiceManager> voice_manager;
    std::unique_ptr<hal::AudioDriver> driver;
    audio::MusicalClock clock;
    audio::TwelveToneEqual tuning;
    std::unordered_map<std::string, int> param_name_to_id;
    int sample_rate;
};

void test_timing_accuracy() {
    std::cout << "--- Testing Timing Accuracy ---" << std::endl;
    
    double sample_rate = 44100.0;
    double bpm = 120.0;
    audio::MusicalClock clock(sample_rate, bpm);
    
    // 1,000,000 samples
    int32_t num_samples = 1000000;
    clock.advance(num_samples);
    
    int64_t expected_ticks = static_cast<int64_t>((static_cast<double>(num_samples) / sample_rate) * (bpm / 60.0) * 960.0);
    
    auto time = clock.current_time();
    std::cout << "Samples: " << num_samples << " (" << (num_samples / sample_rate) << "s)" << std::endl;
    std::cout << "Ticks: " << clock.total_ticks() << " (Expected: " << expected_ticks << ")" << std::endl;
    std::cout << "Time: " << time.bar << "." << time.beat << "." << time.tick << std::endl;
    
    assert(clock.total_ticks() == expected_ticks);
    std::cout << "  Timing Accuracy: OK" << std::endl;
}

void test_frequency_verification() {
    std::cout << "\n--- Testing Frequency Verification ---" << std::endl;
    
    unsigned int sample_rate = 44100;
    EngineHandle handle = engine_create(sample_rate);
    auto engine = static_cast<InternalEngine*>(handle);
    
    // Trigger A4
    engine_note_on_name(handle, "A4", 0.8f);
    
    // Verify TuningSystem output
    audio::Note a4("A4");
    double freq = engine->tuning.get_frequency(a4);
    std::cout << "Note A4 -> Frequency: " << freq << " Hz" << std::endl;
    assert(std::abs(freq - 440.0) < 0.0001);
    
    engine_destroy(handle);
    std::cout << "  Frequency Verification: OK" << std::endl;
}

void test_note_edge_cases() {
    std::cout << "\n--- Testing Note Edge Cases ---" << std::endl;
    
    auto test_note = [](const char* name, bool expect_success, int expected_midi = -1) {
        try {
            audio::Note n(name);
            std::cout << "Note '" << name << "' -> MIDI " << n.midi_note();
            if (!expect_success) {
                std::cout << " (Expected failure but succeeded!)" << std::endl;
                assert(false);
            }
            if (expected_midi != -1) {
                assert(n.midi_note() == expected_midi);
            }
            std::cout << " OK" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Note '" << name << "' -> Error: " << e.what();
            if (expect_success) {
                std::cout << " (Expected success but failed!)" << std::endl;
                assert(false);
            }
            std::cout << " (Expected)" << std::endl;
        }
    };
    
    test_note("C-1", true, 0);   // MIDI 0
    test_note("G9", true, 127);  // MIDI 127
    
    test_note("H#4", false);
    test_note("Banana", false);
    test_note("", false);
    
    std::cout << "  Note Edge Cases: OK" << std::endl;
}

int main() {
    try {
        test_timing_accuracy();
        test_frequency_verification();
        test_note_edge_cases();
        std::cout << "\n=== All Validations Passed! ===" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "\n!!! Validation Failed: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
