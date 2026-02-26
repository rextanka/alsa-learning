# C-Bridge Integration Guide

This guide provides a quick start for application programmers (Swift, C#, Rust, etc.) using the C-compatible bridge to interact with the block-based DSP engine.

## 1. Initialization & Life Cycle

The engine is managed via an opaque `EngineHandle`.

```c
#include "CInterface.h"

// 1. Create the engine at a specific sample rate
EngineHandle engine = engine_create(44100);

// 2. The main audio callback (to be called by your OS audio driver)
void audio_callback(float* buffer, size_t frames) {
    engine_process(engine, buffer, frames);
}

// 3. Clean up
engine_destroy(engine);
```

## 2. Polyphonic Voice Management

The engine supports 16-voice polyphony with LRU (Least Recently Used) voice stealing.

```c
// Trigger a note (MIDI note number, velocity 0.0 - 1.0)
engine_note_on(engine, 60, 0.8f);

// Trigger a note with specific stereo placement (-1.0 Left to 1.0 Right)
engine_set_note_pan(engine, 60, 0.5f);

// Release a note (triggers the envelope Release phase)
engine_note_off(engine, 60);
```

## 3. Parameter Control (`set_param`)

The generic `set_param` function allows you to control internal DSP parameters using string identifiers.

### Valid Parameter Strings:

| Component | Parameter | Range | Description |
|-----------|-----------|-------|-------------|
| **ADSR**  | "attack"  | 0.001+ | Attack time in seconds |
|           | "decay"   | 0.001+ | Decay time in seconds |
|           | "sustain" | 0.0 - 1.0 | Sustain level (amplitude) |
|           | "release" | 0.001+ | Release time in seconds |
| **AD**    | "attack"  | 0.001+ | Attack time (percussive) |
|           | "decay"   | 0.001+ | Decay time (percussive) |
| **Filter**| "cutoff"  | 20 - 20k | Cutoff frequency in Hz |
|           | "resonance"| 0.0 - 1.0 | Feedback resonance |
| **Delay** | "time"    | 0.001+ | Delay time in seconds |
|           | "feedback"| 0.0 - 0.99| Feedback coefficient |
|           | "mix"     | 0.0 - 1.0 | Wet/Dry mix |

## 4. Telemetry API

The engine provides an RT-safe logging system for debugging and testing.

| Function | Parameters | Description |
|----------|------------|-------------|
| `audio_log_message` | `tag, message` | Log a static message. |
| `audio_log_event`   | `tag, value`   | Log a numeric event. |

### Logging Best Practices:
-   **No Format Strings**: `audio_log_message` only takes static strings. Formatting strings in the audio callback is NOT RT-safe.
-   **Value Tagging**: Use `audio_log_event` to send dynamic numeric data (e.g. current filter cutoff, envelope level). This is much more efficient than string conversion.
-   **Tag Consistency**: Use consistent tags (e.g. "Voice", "ADSR", "Filter") to make it easier to filter logs in the host application or test suite.

## 5. Bridge Objects (Handles)

You can also create and manage individual DSP components if you are building your own graph externally.

```c
// Create a standalone Wavetable Oscillator
OscillatorHandle osc = oscillator_create(OSC_WAVETABLE_SINE, 44100);
oscillator_set_frequency(osc, 440.0);
oscillator_process(osc, buffer, frames);
oscillator_destroy(osc);

// Create a standalone ADSR Envelope
EnvelopeHandle env = envelope_create(ENV_ADSR, 44100);
set_param(env, "attack", 0.1f);
envelope_gate_on(env);
envelope_process(env, buffer, frames);
envelope_destroy(env);
```

## 6. Host & Device Interrogation

The bridge provides a safe, "Count and Index" API for discovering hardware capabilities without complex memory management.

```c
// 1. Query device count
int count = host_get_device_count();

for (int i = 0; i < count; ++i) {
    char name[256];
    // 2. Query device name by index (UTF-8)
    if (host_get_device_name(i, name, sizeof(name)) == 0) {
        // 3. Query native sample rate
        int sr = host_get_device_sample_rate(i);
        printf("Device %d: %s (%d Hz)\n", i, name, sr);
    }
}
```

### Safety Features:
- **Unicode Compliance**: All device names are returned as UTF-8 encoded `char*` strings, ensuring support for international characters (Chinese, Korean, etc.).
- **Memory Safety**: The caller provides the buffer for `host_get_device_name`, avoiding ownership traps or leaked pointers.

## 7. Modular Modulation Matrix (Phase 12+)

The engine features a generic `ModulationMatrix` per voice, supporting bipolar intensity and exponential scaling for Pitch and Cutoff.

### Modulation Sources (Internal)
- `MOD_SRC_ENVELOPE` (0): The ADSR envelope of the voice.
- `MOD_SRC_LFO` (1): The internal LFO of the voice.
- `MOD_SRC_VELOCITY` (2): MIDI Velocity.
- `MOD_SRC_AFTERTOUCH` (3): MIDI Aftertouch.

### Modulation Targets
- `MOD_TGT_PITCH` (0): Oscillator frequency (exponential, octaves). Formula: $f_{final} = f_{base} \cdot 2^{mod}$.
- `MOD_TGT_CUTOFF` (1): Filter cutoff frequency (exponential, octaves).
- `MOD_TGT_RESONANCE` (2): Filter resonance (linear offset).
- `MOD_TGT_AMPLITUDE` (3): Final output gain (linear factor).
- `MOD_TGT_PULSEWIDTH` (4): Pulse width offset (linear).

### UI Parameter Discovery (Phase 13)
To avoid hardcoding IDs in the UI, the engine supports discovery by name string. The UI can query the engine for available parameters once to build its mapping.

| Name | Description | ID (mapped in Bridge) |
|------|-------------|----|
| `vcf_cutoff` | VCF Cutoff Frequency | 1 |
| `vcf_res` | VCF Resonance | 2 |
| `osc_pw` | Pulse Width (VCO) | 10 |
| `sub_gain` | Sub-Oscillator Volume | 11 |
| `saw_gain` | Sawtooth Volume | 12 |
| `pulse_gain` | Pulse Volume | 13 |

### Patch Management
The engine supports loading and saving full synth patches in a human-readable JSON format.

```c
// Save current engine state to JSON
engine_save_patch(handle, "my_preset.json");

// Load engine state from JSON
engine_load_patch(handle, "my_preset.json");
```

### API Functions
```c
/**
 * Set a modulation connection.
 * intensity: modulation depth in octaves (pitch/cutoff) or linear (res/amp).
 * Supports bipolar (negative) values for inversion.
 */
int engine_set_modulation(EngineHandle handle, int source, int target, float intensity);
```

Example: Set Vibrato (Internal LFO -> Pitch) with 0.1 octave depth.
```c
engine_set_modulation(engine, MOD_SRC_LFO, MOD_TGT_PITCH, 0.1f);
```

## 8. Modular Routing (External/Legacy)

The engine also supports dynamic modular connections between external processors.

### Processor Types
- `PROC_OSCILLATOR` (0)
- `PROC_LFO` (1)
- `PROC_FILTER` (2)
- `PROC_ENVELOPE` (3)

### Generic Modulation Parameters (Legacy)
- `PARAM_PITCH` (0)
- `PARAM_CUTOFF` (1)
- `PARAM_AMPLITUDE` (2)
- `PARAM_RESONANCE` (3)

### Creating and Linking
```c
// Create a standalone LFO
int lfo_id = engine_create_processor(engine, PROC_LFO);

// Connect LFO to modulate pitch of ALL active voices
engine_connect_mod(engine, lfo_id, ALL_VOICES, PARAM_PITCH, 0.02f);
```

### Auditing
```c
char report[1024];
engine_get_modulation_report(engine, report, sizeof(report));
printf("%s\n", report);
```
>>>>>>> SEARCH

