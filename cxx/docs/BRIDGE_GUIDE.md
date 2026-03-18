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

// Override stereo placement for a note (-1.0 Left to 1.0 Right)
// Call before or after note_on — takes effect on next engine_process
engine_set_note_pan(engine, 60, 0.5f);

// Release a note
engine_note_off(engine, 60);
```

> **Note**: `engine_note_on_group` (voice group splits/layers) and `engine_all_notes_off` / `engine_is_idle` are planned for Phase 17. Use `engine_stop` + `engine_start` to silence all voices between patch loads.

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
| `osc_pw` | `VCO` | Pulse width (legacy alias for `pulse_width`) | 0.0–0.5 |
| `osc_frequency` | `VCO` | Override oscillator base frequency directly | Hz |
| `drawbar_16` | `ORGAN` | 16' Sub-Octave drawbar | 0.0–8.0 |
| `drawbar_513` | `ORGAN` | 5⅓' Quint drawbar | 0.0–8.0 |
| `drawbar_8` | `ORGAN` | 8' Principal drawbar | 0.0–8.0 |
| `drawbar_4` | `ORGAN` | 4' Octave drawbar | 0.0–8.0 |
| `drawbar_223` | `ORGAN` | 2⅔' Nazard drawbar | 0.0–8.0 |
| `drawbar_2` | `ORGAN` | 2' Super-Octave drawbar | 0.0–8.0 |
| `drawbar_135` | `ORGAN` | 1⅗' Tierce drawbar | 0.0–8.0 |
| `drawbar_113` | `ORGAN` | 1⅓' Larigot drawbar | 0.0–8.0 |
| `drawbar_1` | `ORGAN` | 1' Sifflöte drawbar | 0.0–8.0 |

**Implementation note**: `set_param` resolves labels via two paths. Core synthesis parameters (`vcf_*`, `amp_*`, oscillator gains, `osc_pw`, `osc_frequency`, `wavetable_type`) are in a hardcoded fast-path map and bypass the module registry. Organ drawbar labels (`drawbar_*`) go through a `set_parameter_by_name` fallback that queries the ModuleRegistry. Both paths are functionally equivalent from the caller's perspective.

**Status note**: This registry is a transitional compatibility layer retained for single-VCO patch compatibility. Parameters are declared on modules and queryable via `engine_get_module_parameter` (see §4). For multi-module patches use tag-keyed parameter addressing in the patch JSON directly (see PATCH_SPEC.md §Parameters Object). Generating this table from module declarations is a future tooling task.

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

Signal chains are built from the C API. Chain construction must complete before notes are played. All 16 voices share the same topology.

```c
// Add modules in signal-chain order (generator first)
engine_add_module(engine, "COMPOSITE_GENERATOR", "VCO");
engine_add_module(engine, "MOOG_FILTER",         "VCF");
engine_add_module(engine, "ADSR_ENVELOPE",       "ENV");
engine_add_module(engine, "VCA",                 "VCA");

// Declare explicit port connections (audio + CV)
engine_connect_ports(engine, "VCO", "audio_out",    "VCF", "audio_in");
engine_connect_ports(engine, "VCF", "audio_out",    "VCA", "audio_in");
engine_connect_ports(engine, "ENV", "envelope_out", "VCA", "gain_cv");

// Validate topology and activate
int result = engine_bake(engine);
// result == 0 on success, -1 if validation fails
```

`engine_bake` validates:
- First node outputs `PORT_AUDIO`
- Last node outputs `PORT_AUDIO`
- No consecutive `PORT_CONTROL` nodes
- All named ports exist on declared modules
- All connection port types match (`PORT_AUDIO`→`PORT_AUDIO`, `PORT_CONTROL`→`PORT_CONTROL`)
- All referenced module tags exist in the chain
- Port directions are correct (output → input only)
- Lifecycle ports (`gate_in`, `trigger_in`) may not be used in explicit connections

---

## 6. Patch Format v2 — Multi-Group JSON

A patch file describes one or more voice group topologies, their connections, and initial parameters. The `"version": 2` field is required.

```json
{
  "version": 2,
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO" },
        { "type": "MOOG_FILTER",         "tag": "VCF" },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV" },
        { "type": "VCA",                 "tag": "VCA" }
      ],
      "connections": [
        { "from_tag": "VCO", "from_port": "audio_out",    "to_tag": "VCF", "to_port": "audio_in"  },
        { "from_tag": "VCF", "from_port": "audio_out",    "to_tag": "VCA", "to_port": "audio_in"  },
        { "from_tag": "ENV", "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv"   }
      ],
      "parameters": {
        "VCO": { "saw_gain": 1.0 },
        "VCF": { "cutoff": 800.0, "resonance": 0.3 },
        "ENV": { "attack": 0.01, "sustain": 0.8, "release": 0.3 }
      }
    }
  ]
}
```

> **Note**: `parameters` are keyed by instance tag then by the module's parameter name (as declared in MODULE_DESC.md). This is distinct from the `set_param` string labels in §3, which are a legacy C-API shorthand for the most common single-VCO patches. For multi-oscillator or multi-envelope patches use the tag-keyed format exclusively. See PATCH_SPEC.md for full format documentation.

### Feedback Connections

Delay feedback must be marked explicitly:

```json
{ "from_tag": "DELAY", "from_port": "audio_out", "to_tag": "DELAY", "to_port": "audio_in", "intensity": 0.4, "feedback": true }
```

### Loading a Patch

```c
// Load patch — returns 0 on success, -1 on file/parse error
int result = engine_load_patch(engine, "/path/to/sh_bass.json");
```

If the patch topology for a group matches the current chain (same module types and tags in the same order), the chain is **not rebuilt** — only parameters and connections are updated. This avoids audio interruption when switching between patches that share a topology.

---

## 7. Runtime Modulation (Phase 15A LFO API)

LFO modulation routes are configured via the `engine_set_lfo_*` family of functions (Phase 15A). These drive the per-voice internal LFO and ModulationMatrix directly.

```c
// Configure LFO: 5 Hz sine wave with vibrato (±0.05 octave ≈ ±3 semitones)
engine_set_lfo_rate(engine, 5.0f);
engine_set_lfo_waveform(engine, LFO_WAVEFORM_SINE);
engine_set_lfo_depth(engine, LFO_TARGET_PITCH, 0.05f);
engine_set_lfo_intensity(engine, 1.0f);

// LFO → filter cutoff
engine_set_lfo_depth(engine, LFO_TARGET_CUTOFF, 0.3f);

// LFO → pulse width
engine_set_lfo_depth(engine, LFO_TARGET_PULSEWIDTH, 0.2f);

// Clear all LFO routes (restores default Envelope→Amplitude connection)
engine_clear_modulations(engine);
```

**Waveform constants** (`LFO_WAVEFORM_*`): `SINE=0`, `TRIANGLE=1`, `SQUARE=2`, `SAW=3`.

**Target constants** (`LFO_TARGET_*`): `PITCH=0`, `CUTOFF=1`, `RESONANCE=2`, `AMPLITUDE=3`, `PULSEWIDTH=4`.

`engine_set_lfo_depth` may be called multiple times with different targets to route the LFO to several destinations simultaneously. `engine_clear_modulations` resets all routes and LFO intensity to zero, then re-instates the mandatory `Envelope→Amplitude` connection.

> **Phase 16 (complete)**: The integer-ID external modulation matrix (`engine_connect_mod`, `MOD_SRC_*`, `MOD_TGT_*`) was deprecated in Phase 15A and removed in Phase 16. All modulation routes are now first-class named port connections in the patch graph via `engine_connect_ports`.

---

## 8. Implementation Rules

* **C API only for functional tests**: Functional tests use only `CInterface.h`. No C++ headers or internal types. If a behaviour cannot be exercised through the C API it is not a supported feature.
* **RT-Safety**: Bridge lookups are read-only after engine creation. No mutex locks on the audio thread.
* **Sample rate**: Always query hardware (`host_get_device_sample_rate`). Never pass a hardcoded value to `engine_create`.
* **Chain construction order**: `engine_add_module` → `engine_connect_ports` → `engine_bake`. Calling `engine_note_on` before `engine_bake` returns an error.
* **Topology reuse**: If `engine_load_patch` detects that the new patch's chain topology matches the current one for a group, it skips teardown and only applies parameter and connection changes.
* **Parameter source of truth**: All parameter names and target tags are defined and governed by MODULE_DESC.md.

---

## 9. Removed / Deprecated API

| Function | Status | Replacement |
|----------|--------|-------------|
| `engine_set_modulation(source_int, target_int, intensity)` | **Removed** (Phase 15) | `engine_connect_ports(from_tag, from_port, to_tag, to_port)` |
| `VoiceFactory::createSH101()` (C++ internal) | **Removed** (Phase 15) | `engine_load_patch` or `engine_add_module` / `engine_connect_ports` / `engine_bake` |
| `engine_save_patch(handle, path)` | **Removed** (Phase 15) | No replacement — patch serialisation is not implemented. Patches are authored by hand and loaded via `engine_load_patch`. |
| `engine_connect_mod(handle, src, voices, tgt, intensity)` | **Removed** (Phase 16) | `engine_set_lfo_depth(handle, LFO_TARGET_*, depth)` |
| `engine_create_processor(handle, type)` | **Removed** (Phase 16) | `engine_add_module(handle, type, tag)` |
| `engine_get_modulation_report(handle, buf, size)` | **Removed** (Phase 16) | No replacement; use `engine_set_lfo_*` and structured patch files. |
| `MOD_SRC_*` / `MOD_TGT_*` / `ALL_VOICES` constants | **Removed** (Phase 16) | `LFO_TARGET_*` constants |
