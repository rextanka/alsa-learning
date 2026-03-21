/**
 * @file patch_test.cpp
 * @brief Generic patch + MIDI test driver.
 *
 * Loads a patch JSON file and a companion SMF MIDI file, then either:
 *
 *   Default mode  — opens the audio HAL and plays back in real time.
 *                   Use for manual listening and signal-chain inspection.
 *
 *   --smoke mode  — renders offline (no audio hardware), asserts that the
 *                   output is non-silent.  Exits 0 on pass, 1 on failure.
 *                   Used by ctest for automated regression checks.
 *
 * Usage:
 *   patch_test --patch <json>  --midi <mid>  [--smoke]
 *   patch_test -h | --help
 *
 * Signal chain display:
 *   Before playback the program prints the patch name, chain topology, and
 *   connections in a minimal text format derived from the patch JSON.
 *
 * Examples:
 *   patch_test --patch patches/tom_tom.json --midi midi/tom_tom.mid
 *   patch_test --patch patches/juno_pad.json --midi midi/juno_pad.mid --smoke
 */

#include "../../include/CInterface.h"
#include "../TestHelper.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Signal chain display
// ---------------------------------------------------------------------------

static void print_chain(const std::string& patch_path) {
    std::ifstream f(patch_path);
    if (!f.is_open()) {
        std::cerr << "  [cannot open " << patch_path << " for display]\n";
        return;
    }

    json j;
    try { f >> j; } catch (...) {
        std::cerr << "  [cannot parse patch JSON for display]\n";
        return;
    }

    const std::string name    = j.value("name", "(unnamed)");
    const int         version = j.value("version", 0);

    std::cout << "\n";
    std::cout << "Patch: \"" << name << "\"  (" << patch_path << "  v" << version << ")\n";
    std::cout << std::string(60, '-') << "\n";

    if (!j.contains("groups") || j["groups"].empty()) {
        std::cout << "  (no groups)\n";
        return;
    }

    for (const auto& grp : j["groups"]) {
        const int id = grp.value("id", 0);
        std::cout << "Group " << id << " — chain:\n";

        // Nodes
        if (grp.contains("chain")) {
            for (const auto& node : grp["chain"]) {
                std::string tag  = node.value("tag",  "?");
                std::string type = node.value("type", "?");
                std::cout << "  [" << tag << "]  " << type << "\n";
            }
        }

        // Connections
        if (grp.contains("connections") && !grp["connections"].empty()) {
            std::cout << "Connections:\n";
            for (const auto& c : grp["connections"]) {
                std::string ft = c.value("from_tag",  "?");
                std::string fp = c.value("from_port", "?");
                std::string tt = c.value("to_tag",    "?");
                std::string tp = c.value("to_port",   "?");
                std::cout << "  " << ft << "." << fp
                          << "  ──▶  "
                          << tt << "." << tp << "\n";
            }
        }

        // Post chain (v3)
        if (j.contains("post_chain") && !j["post_chain"].empty()) {
            std::cout << "Post-chain (global):\n";
            for (const auto& fx : j["post_chain"]) {
                std::string tag  = fx.value("tag",  "?");
                std::string type = fx.value("type", "?");
                std::cout << "  [" << tag << "]  " << type << "\n";
            }
        }
    }

    // Voice mode / count (v3)
    if (j.contains("voice_mode") || j.contains("voice_count")) {
        std::cout << "Voice: "
                  << j.value("voice_mode", "poly")
                  << "  count=" << j.value("voice_count", 16) << "\n";
    }

    std::cout << std::string(60, '-') << "\n\n";
}

// ---------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------

static void print_help(const char* argv0) {
    std::cout <<
        "Usage:\n"
        "  " << argv0 << " --patch <json> --midi <mid> [--smoke]\n"
        "  " << argv0 << " -h | --help\n"
        "\n"
        "Options:\n"
        "  --patch <file>   Patch JSON file (e.g. patches/tom_tom.json)\n"
        "  --midi  <file>   SMF MIDI file   (e.g. midi/tom_tom.mid)\n"
        "  --smoke          Offline render mode: no audio hardware, exits 0=pass 1=fail\n"
        "  -h, --help       Show this help and exit\n"
        "\n"
        "Default mode (no --smoke):\n"
        "  Opens the audio HAL and plays back the MIDI sequence in real time.\n"
        "  Prints the signal chain topology before playback starts.\n"
        "  Waits for MIDI playback to complete plus a 1-second reverb tail, then exits.\n"
        "\n"
        "Smoke mode (--smoke):\n"
        "  Renders offline until MIDI playback is complete plus a 0.5-second tail.\n"
        "  Asserts that peak output amplitude > 1e-4.  Used by ctest.\n"
        "  Exit 0 = pass, Exit 1 = fail.\n"
        "\n"
        "Signal chain display:\n"
        "  Both modes print the patch chain topology (nodes + connections) before\n"
        "  playback.  The display is derived from the patch JSON — no hardware\n"
        "  interaction required to display the chain.\n"
        "\n"
        "Examples:\n"
        "  patch_test --patch patches/tom_tom.json --midi midi/tom_tom.mid\n"
        "  patch_test --patch patches/juno_pad.json --midi midi/juno_pad.mid --smoke\n";
}

// ---------------------------------------------------------------------------
// Smoke render (offline, no HAL)
// ---------------------------------------------------------------------------

static int run_smoke(EngineHandle engine, int sample_rate,
                     const std::string& patch_path, const std::string& midi_path) {
    std::cout << "[smoke] Loading patch: " << patch_path << "\n";
    if (engine_load_patch(engine, patch_path.c_str()) != 0) {
        std::cerr << "[smoke] FAIL: engine_load_patch returned error\n";
        return 1;
    }

    std::cout << "[smoke] Loading MIDI:  " << midi_path << "\n";
    if (engine_load_midi(engine, midi_path.c_str()) != 0) {
        std::cerr << "[smoke] FAIL: engine_load_midi returned error\n";
        return 1;
    }

    engine_midi_play(engine);

    constexpr size_t FRAMES = 512;
    std::vector<float> buf(FRAMES * 2);

    float peak = 0.0f;
    int   idle_blocks = 0;
    const int TAIL_BLOCKS = static_cast<int>(0.5 * sample_rate / FRAMES) + 1;

    // Render until MIDI is done + tail
    for (;;) {
        engine_process(engine, buf.data(), FRAMES);

        for (size_t i = 0; i < FRAMES * 2; ++i)
            peak = std::max(peak, std::abs(buf[i]));

        if (!engine_midi_is_playing(engine)) {
            ++idle_blocks;
            if (idle_blocks >= TAIL_BLOCKS) break;
        }
    }

    std::cout << "[smoke] Peak amplitude: " << peak << "\n";

    constexpr float MIN_PEAK = 1e-4f;
    if (peak < MIN_PEAK) {
        std::cerr << "[smoke] FAIL: output silent (peak " << peak
                  << " < " << MIN_PEAK << ")\n";
        return 1;
    }

    std::cout << "[smoke] PASS\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Audible playback (real-time HAL)
// ---------------------------------------------------------------------------

static int run_audible(EngineHandle engine,
                       const std::string& patch_path, const std::string& midi_path) {
    std::cout << "[play] Loading patch: " << patch_path << "\n";
    if (engine_load_patch(engine, patch_path.c_str()) != 0) {
        std::cerr << "[play] ERROR: engine_load_patch failed\n";
        return 1;
    }

    std::cout << "[play] Loading MIDI:  " << midi_path << "\n";
    if (engine_load_midi(engine, midi_path.c_str()) != 0) {
        std::cerr << "[play] ERROR: engine_load_midi failed\n";
        return 1;
    }

    if (engine_start(engine) != 0) {
        std::cerr << "[play] ERROR: engine_start failed (no audio device?)\n";
        return 1;
    }

    engine_midi_play(engine);
    std::cout << "[play] Playing...  (Ctrl-C to stop early)\n";

    // Wait for MIDI to finish
    while (engine_midi_is_playing(engine))
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Let reverb tail ring out (1 second)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    engine_stop(engine);
    std::cout << "[play] Done.\n";
    return 0;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    std::string patch_path;
    std::string midi_path;
    bool        smoke  = false;
    bool        help   = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            help = true;
        } else if ((a == "--patch") && i + 1 < argc) {
            patch_path = argv[++i];
        } else if ((a == "--midi") && i + 1 < argc) {
            midi_path = argv[++i];
        } else if (a == "--smoke") {
            smoke = true;
        } else {
            std::cerr << "Unknown argument: " << a << "\n";
            print_help(argv[0]);
            return 1;
        }
    }

    if (help) { print_help(argv[0]); return 0; }

    if (patch_path.empty() || midi_path.empty()) {
        std::cerr << "Error: --patch and --midi are required.\n\n";
        print_help(argv[0]);
        return 1;
    }

    // Always show chain
    print_chain(patch_path);

    // Create engine
    test::init_test_environment();
    const int sample_rate = test::get_safe_sample_rate(0);
    test::EngineWrapper engine_wrapper(sample_rate);
    EngineHandle engine = engine_wrapper.get();

    if (smoke)
        return run_smoke(engine, sample_rate, patch_path, midi_path);
    else
        return run_audible(engine, patch_path, midi_path);
}
