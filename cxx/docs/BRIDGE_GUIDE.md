# C-Bridge Integration & Parameter Mapping Guide

This document serves as the contract between high-level application hosts (Swift, C#, etc.) and the internal C++ DSP engine.

## 1. Initialization & Life Cycle

The engine is managed via an opaque `EngineHandle`.

```c
#include "CInterface.h"

// 1. Create the engine at a specific sample rate
EngineHandle engine = engine_create(44100);

// 2. The main audio callback (called by your OS audio driver)
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

## 3. Tag-Based Parameter Mapping (`set_param`)

The system has transitioned to a **Tag-based resolution model**. The `AudioBridge` resolves string labels from external hosts to internal nodes using the registry below.

### Standardized Node Tags

| Tag | Description |
| --- | --- |
| `VCO` | Primary Oscillators (Sine, Saw, Pulse, Sub) |
| `VCF` | Filter section (Cutoff, Resonance) |
| `VCA` | Amplifier and main ADSR envelope |

### Registry of String Labels

Use these labels with `engine_set_param(handle, "label", value)`:

| String Label | Target Tag | Description | Range |
| --- | --- | --- | --- |
| `vcf_cutoff` | `VCF` | Filter cutoff frequency | Log (20Hz-20kHz) |
| `vcf_res` | `VCF` | Filter resonance | 0.0 - 1.0 |
| `vcf_env_amount` | `VCF` | Envelope to Filter mod depth | Bipolar |
| `amp_attack` | `VCA` | ADSR Attack | Time (ms) |
| `amp_decay` | `VCA` | ADSR Decay | Time (ms) |
| `amp_sustain` | `VCA` | ADSR Sustain | 0.0 - 1.0 |
| `amp_release` | `VCA` | ADSR Release | Time (ms) |
| `sub_gain` | `VCO` | Sub-oscillator level | 0.0 - 1.0 |
| `saw_gain` | `VCO` | Sawtooth level | 0.0 - 1.0 |
| `pulse_gain` | `VCO` | Pulse level | 0.0 - 1.0 |
| `pulse_width` | `VCO` | Pulse width duty cycle | 0.0 - 0.5 |
| `noise_gain` | `VCO` | White noise level | 0.0 - 1.0 |

## 4. Modular Routing (Dynamic Graph)

The engine supports dynamic modular connections between processors.

### Processor Types

* `PROC_OSCILLATOR` (0)
* `PROC_LFO` (1)
* `PROC_FILTER` (2)
* `PROC_ENVELOPE` (3)
* `PROC_AUDIOTAP` (4)

### Creating and Linking

The `engine_connect_mod` function accepts either a string label or a `MOD_TGT_*` enum for the target parameter. Both resolve through the same registry defined in ARCH_PLAN.md.

```c
// Create a standalone LFO
int lfo_id = engine_create_processor(engine, PROC_LFO);

// Connect using string label
engine_connect_mod(engine, lfo_id, ALL_VOICES, "vcf_cutoff", 0.02f);

// Connect using enum (equivalent)
engine_connect_mod(engine, MOD_SRC_LFO, ALL_VOICES, MOD_TGT_CUTOFF, 0.02f);

```

## 5. Modulation & Auditing

Modulation intensity supports negative values for inversion (e.g., closing a filter).

```c
// DEPRECATED: engine_set_modulation is deprecated. Use engine_connect_mod instead.
// int status = engine_set_modulation(engine, source_id, target_id, intensity);

// Preferred: engine_connect_mod (string label or enum target)
engine_connect_mod(engine, MOD_SRC_ENVELOPE, ALL_VOICES, MOD_TGT_AMPLITUDE, 1.0f);

// Audit modular graph
char report[1024];
engine_get_modulation_report(engine, report, sizeof(report));
printf("%s\n", report);

```

## 6. Implementation Rules

* **Voice Registration**: Upon construction, every voice must perform a registration handshake:
`voice->register_parameter(GlobalID, Tag, InternalID);`
* **RT-Safety**: Bridge lookups must be read-only after initialization to avoid mutex locks in the audio thread.
* **Buffers**: Use `AudioTap` for non-destructive signal inspection. Ensure all tap operations are read-only pushes.

## 7. Definition Source
* **Parameter Tags:** All parameter tags and target components are defined and governed by MODULE_DESC.md.