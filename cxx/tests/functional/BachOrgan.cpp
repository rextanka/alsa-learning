#include "CInterface.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

/**
 * BWV 578 Subject (G Minor Fugue)
 * Manual hex encoding for real-time playback.
 */
void play_bwv578_subject(EngineHandle engine) {
    std::cout << "--- Playing Bach BWV 578 Subject (British Organ) ---" << std::endl;

    struct MidiMessage {
        std::vector<uint8_t> data;
        int delay_ms;
    };

    // Subject: G4, D5, Bb4, A4, G4, Bb4, A4, G4, F#4, A4, D4
    std::vector<MidiMessage> subject = {
        {{0x90, 67, 100}, 400}, // G4
        {{0x80, 67, 0}, 0},
        {{0x90, 74, 100}, 400}, // D5
        {{0x80, 74, 0}, 0},
        {{0x90, 70, 100}, 400}, // Bb4
        {{0x80, 70, 0}, 0},
        {{0x90, 69, 100}, 200}, // A4
        {{0x80, 69, 0}, 0},
        {{0x90, 67, 100}, 200}, // G4
        {{0x80, 67, 0}, 0},
        {{0x90, 70, 100}, 200}, // Bb4
        {{0x80, 70, 0}, 0},
        {{0x90, 69, 100}, 200}, // A4
        {{0x80, 69, 0}, 0},
        {{0x90, 67, 100}, 200}, // G4
        {{0x80, 67, 0}, 0},
        {{0x90, 66, 100}, 200}, // F#4
        {{0x80, 66, 0}, 0},
        {{0x90, 69, 100}, 200}, // A4
        {{0x80, 69, 0}, 0},
        {{0x90, 62, 100}, 600}, // D4
        {{0x80, 62, 0}, 500}
    };

    for (const auto& msg : subject) {
        if ((msg.data[0] & 0xF0) == 0x90 && msg.data[2] > 0) {
            engine_note_on(engine, msg.data[1], msg.data[2] / 127.0f);
        } else {
            engine_note_off(engine, msg.data[1]);
        }
        
        if (msg.delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(msg.delay_ms));
        }
    }
}

int main() {
    EngineHandle engine = engine_create(44100);
    if (!engine) return 1;

    engine_start(engine);
    
    play_bwv578_subject(engine);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    engine_stop(engine);
    engine_destroy(engine);

    return 0;
}
