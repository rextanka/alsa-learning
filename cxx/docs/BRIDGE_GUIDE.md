# C-Bridge Integration & Parameter Mapping Guide

This document is the contract between high-level application hosts (Swift, C#, Tauri/React, etc.) and the internal C++ DSP engine. All interaction with the engine from a host must use only the functions declared in `include/CInterface.h`.

---

## 1. Initialization & Life Cycle

The engine is managed via an opaque `EngineHandle`. The sample rate must come from a hardware query — never hardcode 44100 or 48000. Supported rates are 44100 Hz and 48000 Hz. If the hardware reports a rate above 48000 Hz the engine negotiates down to 48000 Hz.

```c
#include "CInterface.h"

// 1. Query the hardware for the best supported sample rate
int sample_rate = host_get_device_sample_rate(0);

// 2. Create the engine
EngineHandle engine = engine_create(sample_rate);

// 3. The main audio callback (called by your OS audio driver)
void audio_callback(float* buffer, size_t frames) {
    engine_process(engine, buffer, frames);
}

// 4. Clean up
engine_destroy(engine);
```

---

## 2. Polyphonic Voice Management

The engine supports 16-voice polyphony with LRU (Least Recently Used) voice stealing within each voice group.

```c
// Trigger a note (MIDI note number, velocity 0.0–1.0)
engine_note_on(engine, 60, 0.8f);

// Trigger a note into a specific voice group (for splits/layers)
engine_note_on_group(engine, 60, 0.8f, /*group_id=*/1);

// Trigger a note with specific stereo placement (-1.0 Left to 1.0 Right)
engine_set_note_pan(engine, 60, 0.5f);

// Release a note
engine_note_off(engine, 60);
```

---

## 3. Tag-Based Parameter Mapping (`set_param`)

Parameters are addressed by string label. The `AudioBridge` resolves labels to the correct chain node via the registry below.

### Registry of String Labels

Use these labels with `set_param(handle, "label", value)`:

| String Label | Target Tag | Description | Range |
| --- | --- | --- | --- |
| `vcf_cutoff` | `VCF` | Filter cutoff frequency | Log (20Hz–20kHz) |
| `vcf_res` | `VCF` | Filter resonance | 0.0–1.0 |
| `vcf_env_amount` | `VCF` | Envelope to Filter mod depth | Bipolar |
| `amp_attack` | `VCA` | ADSR Attack | Time (s) |
| `amp_decay` | `VCA` | ADSR Decay | Time (s) |
| `amp_sustain` | `VCA` | ADSR Sustain | 0.0–1.0 |
| `amp_release` | `VCA` | ADSR Release | Time (s) |
| `sub_gain` | `VCO` | Sub-oscillator level | 0.0–1.0 |
| `saw_gain` | `VCO` | Sawtooth level | 0.0–1.0 |
| `pulse_gain` | `VCO` | Pulse level | 0.0–1.0 |
| `pulse_width` | `VCO` | Pulse width duty cycle | 0.0–0.5 |
| `sine_gain` | `VCO` | Sine oscillator level | 0.0–1.0 |
| `triangle_gain` | `VCO` | Triangle oscillator level | 0.0–1.0 |
| `wavetable_gain` | `VCO` | Wavetable oscillator level | 0.0–1.0 |
| `noise_gain` | `VCO` | White noise level | 0.0–1.0 |
| `wavetable_type` | `VCO` | Wavetable waveform type | 0–N (enum) |

**Note**: This registry is a transitional compatibility layer. In Phase 15 and beyond, parameters are declared on modules and queryable via `engine_get_module_parameter`. The string label table above will be generated from module declarations rather than maintained manually.

---

## 4. Module Registry (Phase 15)

The engine publishes all available module types at runtime. Hosts can query the registry to discover what is available — useful for building dynamic patch editors or modular UI.

```c
// How many module types are registered?
int count = engine_get_module_count(engine);

// Get the type name and description for module at index i
char type_name[64], description[256];
engine_get_module_type(engine, i, type_name, sizeof(type_name));
engine_get_module_description(engine, type_name, description, sizeof(description));

// How many ports does this module type have?
int port_count = engine_get_module_port_count(engine, type_name);

// Get port info: name, type ("AUDIO"/"CONTROL"), direction ("IN"/"OUT"), unipolar flag
char port_name[64], port_type[16], port_dir[8];
int unipolar;
engine_get_module_port(engine, type_name, port_index,
                       port_name, sizeof(port_name),
                       port_type, sizeof(port_type),
                       port_dir, sizeof(port_dir),
                       &unipolar);

// How many parameters does this module type have?
int param_count = engine_get_module_parameter_count(engine, type_name);

// Get parameter info: name, label, min, max, default, logarithmic flag
char param_name[64], param_label[128];
float pmin, pmax, pdef; int logarithmic;
engine_get_module_parameter(engine, type_name, param_index,
                            param_name, sizeof(param_name),
                            param_label, sizeof(param_label),
                            &pmin, &pmax, &pdef, &logarithmic);
```

---

## 5. Chain Construction (Phase 15)

Signal chains are built from the C API. Chain construction must complete before notes are played. All 16 voices in a group share the same topology.

```c
int group_id = 0;

// Add modules in signal-chain order (generator first)
engine_add_module(engine, group_id, "COMPOSITE_GENERATOR", "VCO");
engine_add_module(engine, group_id, "MOOG_FILTER",         "VCF");
engine_add_module(engine, group_id, "ADSR_ENVELOPE",       "ENV");
engine_add_module(engine, group_id, "VCA",                 "VCA");

// Declare explicit port connections (audio + CV)
engine_connect_ports(engine, group_id,
                     "VCO", "audio_out",    "VCF", "audio_in",   1.0f);
engine_connect_ports(engine, group_id,
                     "VCF", "audio_out",    "VCA", "audio_in",   1.0f);
engine_connect_ports(engine, group_id,
                     "ENV", "envelope_out", "VCA", "gain_cv",    1.0f);

// Validate topology and activate
int result = engine_bake(engine, group_id);
// result == 0 on success, negative on validation failure
```

`engine_bake` validates:
- First node outputs `PORT_AUDIO`
- Last node outputs `PORT_AUDIO`
- No consecutive `PORT_CONTROL` nodes
- All named ports exist on declared modules
- All connection port types match

---

## 6. Patch Format v2 — Multi-Group JSON

A patch file describes one or more voice group topologies, their connections, and initial parameters. The `"version": 2` field is required.

```json
{
  "version": 2,
  "groups": [
    {
      "group_id": 0,
      "chain": [
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO" },
        { "type": "MOOG_FILTER",         "tag": "VCF" },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV" },
        { "type": "VCA",                 "tag": "VCA" }
      ],
      "connections": [
        { "src_node": "VCO", "src_port": "audio_out",    "dst_node": "VCF", "dst_port": "audio_in",   "intensity": 1.0 },
        { "src_node": "VCF", "src_port": "audio_out",    "dst_node": "VCA", "dst_port": "audio_in",   "intensity": 1.0 },
        { "src_node": "ENV", "src_port": "envelope_out", "dst_node": "VCA", "dst_port": "gain_cv",    "intensity": 1.0 }
      ],
      "parameters": {
        "vcf_cutoff": 800.0,
        "amp_attack": 0.01,
        "amp_sustain": 0.8,
        "amp_release": 0.3
      }
    }
  ]
}
```

### Feedback Connections

Delay feedback must be marked explicitly:

```json
{ "src_node": "DELAY", "src_port": "audio_out", "dst_node": "DELAY", "dst_port": "audio_in", "intensity": 0.4, "feedback": true }
```

### Loading a Patch

```c
// Load patch — returns 0 on success, ERR_VOICES_ACTIVE if any group has active voices
int result = engine_load_patch(engine, "/path/to/sh_bass.json");

// If voices are active, silence them first then retry
if (result == ERR_VOICES_ACTIVE) {
    engine_all_notes_off(engine);
    // wait for voices to become idle (poll engine_is_idle or use a timeout)
    engine_load_patch(engine, "/path/to/sh_bass.json");
}
```

If the patch topology for a group matches the current chain (same module types and tags in the same order), the chain is **not rebuilt** — only parameters and connections are updated. This avoids audio interruption when switching between patches that share a topology.

---

## 7. Runtime Modulation Connections (Phase 15)

Named port connections can be added and removed at runtime (outside the audio thread):

```c
// Add a modulation connection (e.g. LFO → filter cutoff)
engine_connect_ports(engine, group_id,
                     "LFO", "control_out", "VCF", "cutoff_cv", 0.3f);

// Remove a connection
engine_disconnect_ports(engine, group_id,
                        "LFO", "control_out", "VCF", "cutoff_cv");
```

---

## 8. Implementation Rules

* **C API only for functional tests**: Functional tests use only `CInterface.h`. No C++ headers or internal types. If a behaviour cannot be exercised through the C API it is not a supported feature.
* **RT-Safety**: Bridge lookups are read-only after engine creation. No mutex locks on the audio thread.
* **Sample rate**: Always query hardware (`host_get_device_sample_rate`). Never pass a hardcoded value to `engine_create`.
* **Chain construction order**: `engine_add_module` → `engine_connect_ports` → `engine_bake`. Calling `engine_note_on` before `engine_bake` returns an error.
* **Topology reuse**: If `engine_load_patch` detects that the new patch's chain topology matches the current one for a group, it skips teardown and only applies parameter and connection changes.
* **Parameter source of truth**: All parameter names and target tags are defined and governed by MODULE_DESC.md.

---

## 9. Removed API

The following functions are **removed** in Phase 15 and must not be used:

| Removed function | Replacement |
|-----------------|-------------|
| `engine_set_modulation(source_int, target_int, intensity)` | `engine_connect_ports(src_node, src_port, dst_node, dst_port, intensity)` |
| `VoiceFactory::createSH101()` (C++ internal) | `engine_load_patch` or `engine_add_module` / `engine_connect_ports` / `engine_bake` |
