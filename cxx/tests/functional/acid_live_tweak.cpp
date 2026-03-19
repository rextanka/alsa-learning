/**
 * @file acid_live_tweak.cpp
 * @brief Interactive acid filter sweep tool.
 *
 * Loops the Fm acid riff and lets you sweep the DIODE_FILTER cutoff
 * in real-time using keyboard input:
 *
 *   UP   / k  — cutoff up (faster with SHIFT/K)
 *   DOWN / j  — cutoff down (faster with SHIFT/J)
 *   r / R     — resonance up/down
 *   q         — quit
 *
 * Cutoff range: 100–8000 Hz (log steps)
 * Resonance range: 0.0–0.99
 */

#include "../TestHelper.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>
#include <atomic>
#include <termios.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Raw terminal helpers
// ---------------------------------------------------------------------------

static termios g_orig_termios;

static void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &g_orig_termios);
    termios raw = g_orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 1;  // 100ms timeout
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

static void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
}

// Read a single byte, returns -1 if nothing available
static int read_key() {
    unsigned char c = 0;
    if (read(STDIN_FILENO, &c, 1) == 1) return (int)c;
    return -1;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    test::init_test_environment();
    int sample_rate = test::get_safe_sample_rate(0);
    test::EngineWrapper engine(sample_rate);

    if (engine_load_patch(engine.get(), "patches/acid_reverb.json") != 0) {
        std::cerr << "Failed to load patches/acid_reverb.json\n";
        return 1;
    }

    if (engine_start(engine.get()) != 0) {
        std::cerr << "Failed to start engine\n";
        return 1;
    }

    // Wait for CoreAudio to settle
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 120 BPM: 16th = 125ms, gate = 70ms
    constexpr int SIXTEENTH_MS = 125;
    constexpr int GATE_MS      =  70;

    // Fm riff: F2=41 Ab2=44 Bb2=46 C3=48 Eb3=51
    struct Step { int midi; float vel; };
    const Step phrase[] = {
        {41, 0.9f}, {41, 0.65f}, {44, 0.65f}, { 0, 0.0f},
        {48, 0.65f}, {46, 0.65f}, {44, 0.65f}, {41, 0.9f},
        {41, 0.65f}, {51, 0.65f}, {48, 0.65f}, {46, 0.65f},
        {44, 0.9f},  {41, 0.65f}, {43, 0.65f}, {44, 0.65f}
    };
    constexpr int PHRASE_LEN = 16;

    std::atomic<float> cutoff{1200.0f};
    std::atomic<float> resonance{0.88f};
    std::atomic<float> drive{8.0f};
    std::atomic<float> character{0.3f};
    std::atomic<bool>  running{true};

    // Sequencer thread: loops the riff, applies current params each step
    std::thread seq([&]() {
        int step = 0;
        while (running.load()) {
            const auto& s = phrase[step];
            // Apply current params at every step
            engine_set_tag_param(engine.get(), "VCF",  "cutoff",    cutoff.load());
            engine_set_tag_param(engine.get(), "VCF",  "resonance", resonance.load());
            engine_set_tag_param(engine.get(), "DIST", "drive",     drive.load());
            engine_set_tag_param(engine.get(), "DIST", "character", character.load());

            if (s.midi > 0) {
                engine_note_on(engine.get(), s.midi, s.vel);
                std::this_thread::sleep_for(std::chrono::milliseconds(GATE_MS));
                engine_note_off(engine.get(), s.midi);
                std::this_thread::sleep_for(std::chrono::milliseconds(SIXTEENTH_MS - GATE_MS));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(SIXTEENTH_MS));
            }
            step = (step + 1) % PHRASE_LEN;
        }
    });

    enable_raw_mode();

    std::cout << "\n=== Acid Live Tweak ===\n"
              << "  j/k     : cutoff -/+ (small)\n"
              << "  J/K     : cutoff -/+ (large)\n"
              << "  r/R     : resonance -/+\n"
              << "  d/D     : drive -/+\n"
              << "  c/C     : character -/+ (0=soft, 1=hard)\n"
              << "  q       : quit\n\n";

    auto print_state = [&]() {
        std::cout << "\r  cutoff=" << std::fixed << std::setprecision(0) << cutoff.load()
                  << " Hz  res=" << std::setprecision(2) << resonance.load()
                  << "  drive=" << std::setprecision(1) << drive.load()
                  << "  char=" << std::setprecision(2) << character.load()
                  << "    " << std::flush;
    };

    print_state();

    while (running.load() && test::g_keep_running.load()) {
        int key = read_key();
        if (key < 0) continue;

        float c  = cutoff.load();
        float r  = resonance.load();
        float dr = drive.load();
        float ch = character.load();

        switch (key) {
            case 'k': c  = std::min(16000.0f, c  * 1.05f);  break;
            case 'K': c  = std::min(16000.0f, c  * 1.20f);  break;
            case 'j': c  = std::max(100.0f,   c  / 1.05f);  break;
            case 'J': c  = std::max(100.0f,   c  / 1.20f);  break;
            case 'r': r  = std::max(0.0f,     r  - 0.02f);  break;
            case 'R': r  = std::min(1.0f,     r  + 0.02f);  break;
            case 'd': dr = std::max(1.0f,     dr - 1.0f);   break;
            case 'D': dr = std::min(40.0f,    dr + 1.0f);   break;
            case 'c': ch = std::max(0.0f,     ch - 0.05f);  break;
            case 'C': ch = std::min(1.0f,     ch + 0.05f);  break;
            case 'q': running = false; break;
            // Arrow keys send ESC [ A/B/C/D — handle up/down
            case 27: {
                int a = read_key(), b = read_key();
                if (a == '[') {
                    if (b == 'A') c = std::min(16000.0f, c * 1.05f); // up
                    if (b == 'B') c = std::max(100.0f,   c / 1.05f); // down
                }
                break;
            }
            default: break;
        }

        cutoff    = c;
        resonance = r;
        drive     = dr;
        character = ch;
        print_state();
    }

    disable_raw_mode();
    std::cout << "\nStopping...\n";

    running = false;
    seq.join();
    engine_stop(engine.get());
    return 0;
}
