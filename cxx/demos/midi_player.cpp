/**
 * @file midi_player.cpp
 * @brief Demo: play a MIDI file through a patch with real-time audio output.
 *
 * Usage:
 *   midi_player --preset <name>
 *   midi_player --midi <file> --patch <file>
 *   midi_player -h | --help
 *
 * Presets (paths relative to the binary's working directory):
 *   bach_bwv772     Bach BWV 772  — Invention No. 1 in C major  (organ)
 *   handel_gavotte  Handel HWV 491 — Gavotte                    (harpsichord)
 *
 * Ctrl-C stops playback cleanly and exits.
 */

#include "../include/CInterface.h"
#include "../tests/TestHelper.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Ctrl-C handler
// ---------------------------------------------------------------------------

static std::atomic<bool> g_stop{false};
static void on_sigint(int) { g_stop = true; }

// ---------------------------------------------------------------------------
// Signal chain display
// ---------------------------------------------------------------------------

static void print_chain(const std::string& patch_path) {
    std::ifstream f(patch_path);
    if (!f.is_open()) { std::cerr << "  [cannot open " << patch_path << "]\n"; return; }
    json j;
    try { f >> j; } catch (...) { std::cerr << "  [cannot parse patch JSON]\n"; return; }

    std::cout << "\nPatch: \"" << j.value("name", "(unnamed)") << "\""
              << "  (" << patch_path << "  v" << j.value("version", 0) << ")\n"
              << std::string(60, '-') << "\n";

    for (const auto& grp : j.value("groups", json::array())) {
        std::cout << "Group " << grp.value("id", 0) << " — chain:\n";
        for (const auto& node : grp.value("chain", json::array()))
            std::cout << "  [" << node.value("tag","?") << "]  " << node.value("type","?") << "\n";
        if (grp.contains("connections") && !grp["connections"].empty()) {
            std::cout << "Connections:\n";
            for (const auto& c : grp["connections"])
                std::cout << "  " << c.value("from_tag","?") << "." << c.value("from_port","?")
                          << "  ──▶  "
                          << c.value("to_tag","?") << "." << c.value("to_port","?") << "\n";
        }
    }
    if (j.contains("post_chain") && !j["post_chain"].empty()) {
        std::cout << "Post-chain:\n";
        for (const auto& fx : j["post_chain"])
            std::cout << "  [" << fx.value("tag","?") << "]  " << fx.value("type","?") << "\n";
    }
    std::cout << std::string(60, '-') << "\n\n";
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

struct Preset { const char* key; const char* patch; const char* midi; const char* description; };

static const Preset kPresets[] = {
    { "bach_bwv772",
      "patches/organ_drawbar.json",
      "midi/bach/bwv772_invention1.mid",
      "Bach BWV 772 — Invention No. 1 in C major (organ)" },
    { "handel_gavotte",
      "patches/harpsichord.json",
      "midi/handel/hwv491_gavotte.mid",
      "Handel HWV 491 — Gavotte (harpsichord)" },
};
static constexpr int kNumPresets = 2;

// ---------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------

static void print_help(const char* argv0) {
    std::cout <<
        "Usage:\n"
        "  " << argv0 << " --preset <name>\n"
        "  " << argv0 << " --midi <file> --patch <file>\n"
        "  " << argv0 << " -h | --help\n"
        "\n"
        "Presets:\n";
    for (const auto& p : kPresets)
        std::cout << "  " << p.key << "\n    " << p.description << "\n";
    std::cout <<
        "\n"
        "Options:\n"
        "  --preset <name>  Use a named preset (see above)\n"
        "  --midi   <file>  SMF MIDI file path\n"
        "  --patch  <file>  Patch JSON file path\n"
        "  -h, --help       Show this help\n"
        "\n"
        "Paths are relative to the binary's working directory.\n"
        "Press Ctrl-C at any time to stop playback cleanly.\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    std::string midi_path;
    std::string patch_path;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") { print_help(argv[0]); return 0; }
        else if (a == "--preset" && i + 1 < argc) {
            const std::string key = argv[++i];
            bool found = false;
            for (const auto& p : kPresets) {
                if (key == p.key) {
                    patch_path = p.patch;
                    midi_path  = p.midi;
                    found = true; break;
                }
            }
            if (!found) {
                std::cerr << "Unknown preset: " << key << "\n\n";
                print_help(argv[0]); return 1;
            }
        } else if (a == "--midi" && i + 1 < argc) {
            midi_path = argv[++i];
        } else if (a == "--patch" && i + 1 < argc) {
            patch_path = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << a << "\n\n";
            print_help(argv[0]); return 1;
        }
    }

    if (midi_path.empty() || patch_path.empty()) {
        std::cerr << "Error: --preset or both --midi and --patch are required.\n\n";
        print_help(argv[0]); return 1;
    }

    std::signal(SIGINT, on_sigint);

    print_chain(patch_path);

    test::init_test_environment();
    const int sample_rate = test::get_safe_sample_rate(0);
    test::EngineWrapper engine_wrapper(sample_rate);
    EngineHandle engine = engine_wrapper.get();

    if (engine_load_patch(engine, patch_path.c_str()) != 0) {
        std::cerr << "ERROR: engine_load_patch failed: " << patch_path << "\n"; return 1;
    }
    if (engine_load_midi(engine, midi_path.c_str()) != 0) {
        std::cerr << "ERROR: engine_load_midi failed: " << midi_path << "\n"; return 1;
    }
    if (engine_start(engine) != 0) {
        std::cerr << "ERROR: engine_start failed (no audio device?)\n"; return 1;
    }

    engine_midi_play(engine);
    std::cout << "[midi_player] Playing: " << midi_path << "\n"
              << "[midi_player] Press Ctrl-C to stop.\n";

    while (!g_stop && engine_midi_is_playing(engine))
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    if (g_stop) {
        std::cout << "\n[midi_player] Interrupted.\n";
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        std::cout << "[midi_player] Done.\n";
    }

    engine_stop(engine);
    return 0;
}
