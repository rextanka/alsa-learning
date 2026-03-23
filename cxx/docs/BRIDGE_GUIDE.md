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

> **Note**: `engine_note_on_group`, `engine_all_notes_off`, and `engine_is_idle` are not yet implemented. Use `engine_stop` + `engine_start` to silence all voices between patch loads.

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

## 4. Module Registry (Phase 15 + Phase 27A)

The engine publishes all available module types at runtime. Hosts can query the registry to discover what is available — useful for building dynamic patch editors or modular UI.

### Legacy enumeration (Phase 15)

```c
// How many module types are registered?
int count = engine_get_module_count(engine);

// Get the type name of module at index i
char type_name[64];
engine_get_module_type(engine, i, type_name, sizeof(type_name));
```

### Full JSON introspection (Phase 27A — preferred)

`module_get_descriptor_json` and `module_registry_get_all_json` supersede the per-field query functions. No `EngineHandle` is required — the registry is populated at static-init time.

```c
// Full descriptor for one module type — returns bytes written, -1 unknown, -2 buf too small
char buf[4096];
int n = module_get_descriptor_json("MOOG_FILTER", buf, sizeof(buf));

// All registered modules as a sorted JSON array
char all[65536];
int n = module_registry_get_all_json(all, sizeof(all));
```

JSON schema per module: `{ "type_name", "role", "brief", "usage_notes", "parameters": [...], "ports": [...] }`.
The `"role"` field (added Phase 27C) is one of `"SOURCE"`, `"SINK"`, or `"PROCESSOR"`, inferred automatically from the module's declared ports:
- `"SOURCE"` — PORT_AUDIO output, no PORT_AUDIO input (e.g. `WHITE_NOISE`, `AUDIO_FILE_READER`, `AUDIO_INPUT`). Use at the chain head.
- `"SINK"` — PORT_AUDIO input, no PORT_AUDIO output (e.g. `AUDIO_OUTPUT`, `AUDIO_FILE_WRITER`). Use as the last chain node only.
- `"PROCESSOR"` — everything else: modules with both PORT_AUDIO in and out (filters, FX, VCA), and pure CV modules with no PORT_AUDIO at all (`LFO`, `ADSR_ENVELOPE`, `CV_MIXER`, `MATHS`, `INVERTER`, etc.). `COMPOSITE_GENERATOR` has a PORT_AUDIO input (`fm_in`) and is therefore also PROCESSOR.
Use `role` in patch editors to filter which modules are valid at each chain position (e.g. offer only SOURCEs at the head, only PROCESSORs or SINKs thereafter).
Works directly with Swift `JSONDecoder` and `JSON.parse` in React/Tauri. See ARCH_PLAN.md §Phase 27A for the full schema.

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
- Last node outputs `PORT_AUDIO`, **or** last node is a SINK-role module (audio_in only, no audio_out — e.g. `AUDIO_OUTPUT`, `AUDIO_FILE_WRITER`)
- No consecutive `PORT_CONTROL` nodes
- All named ports exist on declared modules
- All connection port types match (`PORT_AUDIO`→`PORT_AUDIO`, `PORT_CONTROL`→`PORT_CONTROL`)
- All referenced module tags exist in the chain
- Port directions are correct (output → input only)
- Lifecycle ports (`gate_in`, `trigger_in`) may not be used in explicit connections

---

## 6. Patch Format v2 / v3 — Multi-Group JSON

A patch file describes one or more voice group topologies, their connections, and initial parameters. Version 2 is the baseline format; version 3 extends it with a top-level `post_chain` array for global effects. Both versions are accepted by `engine_load_patch` and `engine_load_patch_json`.

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
// Load from file — returns 0 on success, -1 on file/parse error
int result = engine_load_patch(engine, "/path/to/sh_bass.json");

// Load from in-memory string — pass -1 for json_len to use strlen()
int result = engine_load_patch_json(engine, json_str, (int)strlen(json_str));
```

If the patch topology for a group matches the current chain (same module types and tags in the same order), the chain is **not rebuilt** — only parameters and connections are updated. This avoids audio interruption when switching between patches that share a topology.

---

## 7. Runtime Modulation — Phase 16 Chain-Based LFO

> **`engine_set_lfo_*` and `engine_clear_modulations` were REMOVED in Phase 16.** They are not in `CInterface.h`. See §9 Removed/Deprecated API.

All modulation is now expressed as named port connections in the voice graph. Add an LFO as a chain module and wire its output to any `PORT_CONTROL` input:

```c
// LFO vibrato: LFO → VCO pitch_cv
engine_add_module(engine, "LFO", "LFO1");
engine_add_module(engine, "COMPOSITE_GENERATOR", "VCO");
engine_add_module(engine, "ADSR_ENVELOPE", "ENV");
engine_add_module(engine, "VCA", "VCA");

engine_connect_ports(engine, "LFO1", "control_out", "VCO", "pitch_cv");
engine_connect_ports(engine, "ENV",  "envelope_out", "VCA", "gain_cv");
engine_bake(engine);

// Set LFO parameters via set_param or patch JSON
set_param(engine, "lfo_rate",      5.0f);  // Hz
set_param(engine, "lfo_intensity", 0.003f); // octaves
```

LFO can simultaneously route to multiple destinations by declaring multiple `engine_connect_ports` calls from the same LFO output to different inputs.

---

## 8. Global Post-Processing Chain (Phase 19)

Effects applied after all voices are summed, before the HAL write. Supports `REVERB_FREEVERB`, `REVERB_FDN`, and `PHASER`.

```c
// Append an effect — returns 0-based index, or -1 on unknown type
int idx = engine_post_chain_push(engine, "REVERB_FDN");

// Configure it
engine_post_chain_set_param(engine, idx, "decay",   2.5f);
engine_post_chain_set_param(engine, idx, "wet",     0.4f);
engine_post_chain_set_param(engine, idx, "damping", 0.3f);

// Remove all effects
engine_post_chain_clear(engine);
```

Effects run in push order. Multiple effects are chained in sequence. Parameters are addressed by `fx_index` (0-based, in push order) and parameter name (see MODULE_DESC.md for each type's parameters).

---

## 9. Host Device Enumeration (Phase 20)

All device information comes from the HAL layer. No platform-specific calls are required from the host application.

```c
// How many output-capable devices are on this host?
int n = host_get_device_count();

// Name, default rate, and default block size for device i
char name[256];
host_get_device_name(i, name, sizeof(name));
int sr   = host_get_device_sample_rate(i);
int bs   = host_get_device_block_size(i);

// Supported rates and block sizes for device i
int rates[16], sizes[16];
int rate_count = host_get_supported_sample_rates(i, rates, 16);
int size_count = host_get_supported_block_sizes(i, sizes, 16);

// Queries on an open engine handle
int driver_sr = engine_get_driver_sample_rate(engine);
int driver_bs = engine_get_driver_block_size(engine);
char driver_name[256];
engine_get_driver_name(engine, driver_name, sizeof(driver_name));
```

---

## 10. SMF File Playback (Phase 22A)

Sample-accurate Standard MIDI File playback. All tracks merged; MIDI channel ignored (channel-blind). All voices use the single loaded patch.

```c
// 1. Load a patch for the timbre
engine_load_patch(engine, "patches/organ_drawbar.json");

// 2. Load and parse the MIDI file
int rc = engine_load_midi(engine, "midi/bwv578.mid");
// rc == 0 on success, -1 on parse error

// 3. Start the engine and begin playback
engine_start(engine);
engine_midi_play(engine);

// 4. Control playback
engine_midi_stop(engine);    // pause — playhead stays
engine_midi_rewind(engine);  // stop + seek to start

// 5. Query playhead position
uint64_t tick;
engine_midi_get_position(engine, &tick);

// 6. Poll playback state (useful for offline render loops and audible tests)
int playing = engine_midi_is_playing(engine);
// Returns 1 while events remain to be dispatched, 0 when done or not started.
// MidiFilePlayer auto-stops after the last event is dispatched — no manual
// stop() needed for end-of-file detection.
```

Playback is sample-accurate: `MidiFilePlayer::advance(frames, sr, vm)` is called once per audio block from both the HAL callback and `engine_process`. No `sleep()` or wall-clock timing.

> **Phase 22B (planned)**: Multi-timbral routing — MIDI channel maps to a VoiceGroup with its own patch. Program Change events trigger live patch swaps.

---

## 11. Implementation Rules

* **C API only for functional tests**: Functional tests use only `CInterface.h`. No C++ headers or internal types. If a behaviour cannot be exercised through the C API it is not a supported feature.
* **RT-Safety**: Bridge lookups are read-only after engine creation. No mutex locks on the audio thread.
* **Sample rate**: Always query hardware (`host_get_device_sample_rate`). Never pass a hardcoded value to `engine_create`.
* **Chain construction order**: `engine_add_module` → `engine_connect_ports` → `engine_bake`. Calling `engine_note_on` before `engine_bake` returns an error.
* **Topology reuse**: If `engine_load_patch` detects that the new patch's chain topology matches the current one for a group, it skips teardown and only applies parameter and connection changes.
* **Parameter source of truth**: All parameter names and target tags are defined and governed by MODULE_DESC.md.

---

## 12. Patch Serialization (Phase 27B)

Serialize the current engine state to JSON and reload it — full round-trip fidelity.

```c
// Serialize voice chain + post-chain to a caller buffer.
// Returns bytes written (excl. NUL), -2 if buf too small, -1 on error.
// group_index is reserved for future multi-group support; pass 0.
int engine_get_patch_json(EngineHandle handle, int group_index,
                          char* buf, int max_len);

// Load a patch from an in-memory JSON string (v2 or v3 format).
// json_len may be -1 to use strlen(). Returns 0 on success.
int engine_load_patch_json(EngineHandle handle, const char* json_str, int json_len);

// Serialize and write to a file. Returns 0 on success.
int engine_save_patch(EngineHandle handle, const char* path);
```

### Patch Format v3 — Post-Chain Array

`engine_get_patch_json` always outputs v3. The only addition over v2 is a top-level `post_chain` array; v2 files (no `post_chain` key) continue to load unchanged.

```json
{
  "version": 3,
  "name": "Juno Strings",
  "groups": [ { ... } ],
  "post_chain": [
    { "type": "JUNO_CHORUS", "parameters": { "mode": 2, "rate": 0.5, "depth": 0.6 } }
  ]
}
```

Loading a v3 patch clears any existing post-chain then replaces it with the entries in `post_chain`. Loading a v2 patch clears the post-chain (it has no `post_chain` key).

### Round-Trip Example

```c
// Serialize current patch to a buffer
char buf[65536];
int n = engine_get_patch_json(engine, 0, buf, sizeof(buf));
// n == -2 if buf too small; increase buffer and retry

// Reload from the serialized string — identical to reloading the original file
engine_load_patch_json(engine2, buf, n);

// Or save to disk for later
engine_save_patch(engine, "/tmp/my_patch.json");
```

### Serialization Scope

| Source | What is captured |
|--------|-----------------|
| `signal_chain_` | All PORT_AUDIO nodes (generators, filters, effects) in order |
| `mod_sources_` | PORT_CONTROL generators (LFOs, envelopes) — not reachable from audio output |
| `connections_` | All named port connections (audio-rate and control-rate) |
| Per-node parameters | Every `apply_parameter` call made since the last `engine_bake` |
| `post_chain` | Type name and parameters of every `engine_post_chain_push` entry |

> **Graph traversal alone is insufficient.** `mod_sources_` entries (LFOs wired only to CV inputs) have no audio-path predecessor — the serializer walks both `signal_chain_` and `mod_sources_` explicitly.

---

## 13. I/O Processor API (Phase 27C)

Three new C API functions support the I/O processor family added in Phase 27C.

### `engine_set_tag_string_param`

```c
// Set a string parameter on a tagged processor.
// Returns 0 on success, -1 if the tag is not found.
int engine_set_tag_string_param(EngineHandle handle,
                                const char* tag,
                                const char* name,
                                const char* value);
```

Used to set the `"path"` string parameter on `AUDIO_FILE_READER` and `AUDIO_FILE_WRITER` nodes. Call after `engine_bake`. The tag identifies the chain node (e.g. `"FILE_IN"`, `"FILE_OUT"`).

```c
// Set input file path on a tagged AUDIO_FILE_READER
engine_set_tag_string_param(engine, "FILE_IN",  "path", "/samples/loop.wav");

// Set output file path on a tagged AUDIO_FILE_WRITER
engine_set_tag_string_param(engine, "FILE_OUT", "path", "/tmp/capture.wav");
```

### `engine_open_audio_input`

```c
// Associate an AUDIO_INPUT node with a hardware capture device.
// device_index corresponds to the index used in host_get_device_name / host_get_device_sample_rate.
// Returns 0 on success, -1 on error (device not found or capture not supported).
int engine_open_audio_input(EngineHandle handle, int device_index);
```

Opens the capture PCM path (CoreAudio full-duplex or ALSA `SND_PCM_STREAM_CAPTURE`) and binds it to all `AUDIO_INPUT` chain nodes. Until this is called — or until Phase 25 HAL routing is implemented — `AUDIO_INPUT` outputs silence.

```c
// Open default capture device (index 0)
int rc = engine_open_audio_input(engine, 0);
```

### `engine_file_writer_flush`

```c
// Flush all AUDIO_FILE_WRITER processors — finalises WAV headers and syncs to disk.
// Safe to call while the engine is running; recording continues after the flush.
// Returns 0 on success.
int engine_file_writer_flush(EngineHandle handle);
```

An automatic flush also occurs on `engine_destroy`. Use explicit flush for intermediate checkpoints or before reading a partially-written file.

```c
// Capture 5 seconds, then flush and inspect the file
engine_start(engine);
// ... wait 5 seconds ...
engine_file_writer_flush(engine);
```

### API Reference Summary (Phase 27C additions)

| Function | Description |
|----------|-------------|
| `engine_set_tag_string_param(handle, tag, name, value)` | Set string param (e.g. `"path"`) on a tagged processor |
| `engine_open_audio_input(handle, device_index)` | Connect `AUDIO_INPUT` node to hardware capture device |
| `engine_file_writer_flush(handle)` | Flush all `AUDIO_FILE_WRITER` processors to disk |

---

## 14. Transport Clock & Tempo-Sync Effects (Phase 27D)

The engine owns an authoritative `MusicalClock` that all tempo-sensitive processors read
each block. Two write paths, one read path:

- **Host/UI write** — `engine_set_tempo` / `engine_set_time_signature` set tempo and meter.
- **SMF write** — `MidiFilePlayer` extracts FF 51 Set Tempo meta-events during playback and
  automatically updates the clock. Host-set tempo is overridden while a file is playing.
- **Processor read** — `BlockContext` (a concrete `VoiceContext`) carries `bpm` and
  `beats_per_bar` pre-computed once per block. Processors receive it inside `do_pull()` —
  zero allocations, zero locks.

### New C API

```c
// Set tempo manually. Ignored while an SMF file with FF 51 events is playing.
void engine_set_tempo(EngineHandle handle, float bpm);

// Query current live tempo (reflects SMF tempo if playing, else manually set value).
float engine_get_tempo(EngineHandle handle);

// Set time signature. numerator = beats per bar; denominator reserved for future use.
void engine_set_time_signature(EngineHandle handle, int numerator, int denominator);
```

> The legacy `engine_set_bpm`, `engine_get_bpm`, and `engine_set_meter` remain available
> as aliases but prefer the new names for new code.

### Beat Division Vocabulary

Pass `division` as a float index 0–10 via `engine_post_chain_set_param` or
`engine_set_tag_param`:

| Index | Name | Multiplier | At 120 BPM |
|-------|------|-----------|------------|
| 0 | `whole` | 4.0 | 2000 ms |
| 1 | `half` | 2.0 | 1000 ms |
| 2 | `quarter` | 1.0 | 500 ms |
| 3 | `dotted_quarter` | 1.5 | 750 ms |
| 4 | `eighth` | 0.5 | 250 ms |
| 5 | `dotted_eighth` | 0.75 | 375 ms |
| 6 | `triplet_quarter` | 0.667 | 333 ms |
| 7 | `sixteenth` | 0.25 | 125 ms |
| 8 | `triplet_eighth` | 0.333 | 167 ms |
| 9 | `thirtysecond` | 0.125 | 62.5 ms |
| 10 | `sixtyfourth` | 0.0625 | 31.25 ms |

`delay_time_seconds = (60 / bpm) × multiplier`

### Usage Example — Tempo-Synced Delay

```c
// Build patch (VCO → ENV → VCA)
engine_add_module(engine, "COMPOSITE_GENERATOR", "VCO");
engine_add_module(engine, "ADSR_ENVELOPE",        "ENV");
engine_add_module(engine, "VCA",                  "VCA");
engine_connect_ports(engine, "ENV", "envelope_out", "VCA", "gain_cv");
engine_bake(engine);

// Add synced delay to global post-chain
int dly = engine_post_chain_push(engine, "ECHO_DELAY");
engine_post_chain_set_param(engine, dly, "sync",     1.0f);
engine_post_chain_set_param(engine, dly, "division", 5.0f); // dotted_eighth → "Edge" delay
engine_post_chain_set_param(engine, dly, "feedback", 0.4f);
engine_post_chain_set_param(engine, dly, "mix",      0.35f);

// Set tempo — delay time updates every block automatically
engine_set_tempo(engine, 120.0f);
```

### Synced Processors

| Module | Parameter | Default | Notes |
|--------|-----------|---------|-------|
| `ECHO_DELAY` | `sync` / `division` | 0 / 2 | Delay time derived from clock; `time` param ignored when `sync=1` |
| `LFO` | `sync` / `division` | 0 / 2 | LFO period = one beat-division cycle; `rate` param ignored when `sync=1` |
| `PHASER` | `sync` / `division` | 0 / 1 | Sweep period; `rate` param ignored when `sync=1` |

When `sync=0` (default) behaviour is identical to pre-Phase 27D — fully backward-compatible.

---

## 15. MIDI-to-CV Routing (Phase 27E — Complete)

Phase 27E replaced the implicit lifecycle callbacks (`on_note_on` / `on_note_off`) with a routable `MIDI_CV` source module, giving patch authors full control over how keyboard pitch and gate signals flow through the signal chain. All patches have been migrated; the `Voice` legacy auto-injection code has been removed.

### 15.1  `MIDI_CV` Module

`MIDI_CV` is a `SOURCE`-role module that exposes keyboard state as patchable CV and gate ports.

| Port | Direction | Type | Description |
|------|-----------|------|-------------|
| `pitch_cv` | out | PORT_CONTROL | 1 V/oct pitch CV (C4 = 0 V) |
| `gate_cv` | out | PORT_CONTROL | 0 V / 5 V gate (high while key held) |
| `velocity_cv` | out | PORT_CONTROL | Note-on velocity 0–1 normalized |
| `aftertouch_cv` | out | PORT_CONTROL | Channel aftertouch 0–1 |

Typical wiring:

```json
"chain": [
  { "type": "MIDI_CV",            "tag": "KBD" },
  { "type": "COMPOSITE_GENERATOR","tag": "VCO" },
  { "type": "ADSR_ENVELOPE",      "tag": "ENV" },
  { "type": "VCA",                "tag": "VCA" }
],
"connections": [
  { "from_tag": "KBD", "from_port": "pitch_cv",    "to_tag": "VCO", "to_port": "pitch_base_cv" },
  { "from_tag": "KBD", "from_port": "gate_cv",     "to_tag": "ENV", "to_port": "gate_in"       },
  { "from_tag": "ENV", "from_port": "envelope_out","to_tag": "VCA", "to_port": "gain_cv"       }
]
```

### 15.2  Two-Port Pitch on `COMPOSITE_GENERATOR`

Phase 27E adds a second pitch input port to `COMPOSITE_GENERATOR`:

| Port | Type | Description |
|------|------|-------------|
| `pitch_base_cv` | PORT_CONTROL | Absolute pitch (V/oct) — typically from `MIDI_CV.pitch_cv` |
| `pitch_cv` | PORT_CONTROL | Additive modulation offset (V/oct) — from LFO vibrato, portamento, etc. |

Both inputs sum at the oscillator, mirroring the M-110 hardware where KBD CV and MOD CV arrive on separate summing junctions.

### 15.3  Gate-CV Trigger Model

After Phase 27E, gate triggering is entirely port-driven:

- `ADSR_ENVELOPE.gate_in` replaces the lifecycle `on_note_on` / `on_note_off` callbacks.
- `ADSR_ENVELOPE.ext_gate_in` remains available for secondary trigger sources (LFO square wave for repeating-trigger patches such as banjo).
- `on_note_on` / `on_note_off` are removed from the public C API; `engine_note_on` / `engine_note_off` become thin wrappers that drive `MIDI_CV` state.

### 15.4  New and Revised Ports (Phase 27E)

The following port additions were implemented alongside `MIDI_CV`:

| Module | New Port / Parameter | Description |
|--------|----------------------|-------------|
| `LFO` | `control_out_inv` (out) | Always −1 × `control_out`; eliminates a separate `INVERTER` for counter-phase routing (M-150 OUT B precedent) |
| `ECHO_DELAY` | `time_cv` (in, PORT_CONTROL) | Additive modulation of delay time in seconds (M-172 BBD clock CV precedent) |
| `GATE_DELAY` | `gate_in_b` (in, PORT_CONTROL) | Secondary gate input OR'd with `gate_in` (M-172 INPUT B) |
| `GATE_DELAY` | `gate_time` parameter | 0 = mirror input duration; >0 = fixed output pulse width (0–6 s) |
| `GATE_DELAY` | `delay_time` range | Extended to 6.0 s (was 2.0 s) to match M-172 hardware |
| `CV_MIXER` | `cv_out_inv` (out, PORT_CONTROL) | Inverted sum output — always −1 × `cv_out`; M-132 INV OUT precedent. Eliminates a downstream `INVERTER` when two destinations need opposing polarity from the same mix (e.g. filter opens while VCA closes). Implemented via the `_inv`-suffix convention in `Voice::pull_mono`. |

### 15.5  Roland M-132 Pattern (Phase 27E)

The Roland M-132 is a Dual CV/Audio Mixer + Voltage Processor used on the System 100M to combine keyboard gate pulses, LFO signals, and static bias voltages before routing them to envelope triggers and CV destinations.

Our existing `CV_MIXER` covers the core M-132 summing capability. Phase 27E completes the picture by making the keyboard gate a first-class routable CV via `MIDI_CV.gate_cv`, and adding `CV_MIXER.cv_out_inv` for the inverted sum output.

**The banjo repeating-trigger pattern (Roland Fig 3-4):**

On the original hardware, the M-132 summed:
- A −10 V static bias (keeps the net below trigger threshold)
- The keyboard gate pulse (+gate V), which cancels the bias and lets the LFO square wave through

This meant the LFO only triggered the ADSR while a key was held. Our arch-audit approximation (`LFO.control_out → ADSR.ext_gate_in`) works but is always active. The canonical Phase 27E wiring uses `MIDI_CV.gate_cv` as a real CV multiplier through `CV_MIXER`:

```json
"chain": [
  { "type": "MIDI_CV",            "tag": "KBD" },
  { "type": "LFO",                "tag": "LFO" },
  { "type": "CV_MIXER",           "tag": "MIX" },
  { "type": "COMPOSITE_GENERATOR","tag": "VCO" },
  { "type": "SH_FILTER",          "tag": "VCF" },
  { "type": "ADSR_ENVELOPE",      "tag": "ENV" },
  { "type": "VCA",                "tag": "VCA" }
],
"connections": [
  { "from_tag": "KBD", "from_port": "pitch_cv",    "to_tag": "VCO", "to_port": "pitch_base_cv" },
  { "from_tag": "KBD", "from_port": "gate_cv",     "to_tag": "ENV", "to_port": "gate_in"       },
  { "from_tag": "LFO", "from_port": "control_out", "to_tag": "MIX", "to_port": "cv_in_1"       },
  { "from_tag": "KBD", "from_port": "gate_cv",     "to_tag": "MIX", "to_port": "cv_in_2"       },
  { "from_tag": "MIX", "from_port": "cv_out",      "to_tag": "ENV", "to_port": "ext_gate_in"   },
  { "from_tag": "ENV", "from_port": "envelope_out","to_tag": "VCA", "to_port": "gain_cv"        }
],
"parameters": {
  "MIX": { "gain_1": 1.0, "gain_2": 1.0, "offset": -0.5 }
}
```

`gain_2 × gate_cv` goes high (1.0) only while a key is held; the −0.5 offset keeps the mixed output below the trigger threshold when no key is held, so the LFO can only re-trigger the ADSR while a key is pressed. This faithfully replicates the M-132 bias trick.

**Other M-132 use cases unlocked by `cv_out_inv`:**

- **Counter-phase tremolo**: Route one LFO simultaneously to filter (opens) and VCA (closes) without an extra `INVERTER`. Connect `CV_MIXER.cv_out → VCF.cutoff_cv` and `CV_MIXER.cv_out_inv → VCA.gain_cv`.
- **Auto-wah**: `ENVELOPE_FOLLOWER.envelope_out → CV_MIXER.cv_in_1`; `CV_MIXER.cv_out → VCF.cutoff_cv`. Adjust `offset` to set the resting cutoff.
- **Pitch bend + vibrato sum**: Mix `MIDI_CV.pitch_cv` and `LFO.control_out` into `CV_MIXER`, route the sum to `VCO.pitch_cv` for simultaneous manual bend and LFO vibrato.

### 15.6  Patch Audit Status

The arch-audit (branch `202603201000-arch-audit`) migrated all patches to Phase 27E explicit MIDI_CV routing. All 29 patches below were verified and corrected.

**Migrated (arch-audit 2026-03-21):**

| Patch | Topology | Notes |
|-------|----------|-------|
| `flute.json` | SH_FILTER + ADSR + VCA | LFO vibrato; pitch tracking confirmed |
| `clarinet.json` | SH_FILTER + ADSR + VCA | Pulse wave; register-crossing character |
| `brass.json` / `horn.json` | SH_FILTER + CV_SPLITTER + CV_SCALER + ADSR + VCA | Roland Fig 1-6; CV_SCALER registration bug fixed this session |
| `trumpet.json` | As brass | Brighter cutoff |
| `trombone.json` | As brass | Slower attack |
| `tuba.json` | As brass | Sub-octave saw; lower cutoff |
| `oboe.json` | SH_FILTER + ADSR + VCA | Nasal pulse mix |
| `english_horn.json` | As oboe | Mellower variant |
| `acid_reverb.json` | DIODE_FILTER + AD_ENV + ADSR(GATE) + VCA + DIST → post FDN | Rewritten this session to correct TB-303 two-envelope architecture |
| `banjo.json` | VCO + SH_FILTER + ADSR + VCA, LFO→ext_gate_in | Rewritten this session to Roland Fig 3-4 repeating-trigger topology |

**Infrastructure fix (arch-audit):** `CV_SCALER` was registered only via a static initializer in `CvScalerProcessor.cpp`; the linker dead-code-eliminated the TU. Fixed by adding an explicit `register_module` call in `ProcessorRegistrations.cpp`. All brass-family patches were silently failing to load before this fix.

**Also migrated (Phase 27E pass, 2026-03-21):**

`bass_drum`, `bell`, `bongo_drums`, `bowed_bass`, `cow_bell`, `cymbal`, `delay_lead`, `dog_whistle`, `glockenspiel`, `gong`, `gong_full`, `group_strings`, `harpsichord`, `juno_pad`, `juno_strings`, `organ_drawbar`, `percussion_noise`, `pizzicato_violin`, `rain`, `reverb`, `sh_bass`, `snare_drum`, `tb_bass`, `thunder`, `tom_tom`, `violin_vibrato`, `whistling`, `wind_surf`, `wood_blocks`.

Each patch was reviewed against the Roland service notes and updated to explicit MIDI_CV routing. The `bowed_bass` patch additionally received a new `pw_env_cv` feature on `COMPOSITE_GENERATOR` to model Roland Fig 2-10 bow-contact pulse-width modulation.

---

## 16. Removed / Deprecated API

| Function | Status | Replacement |
|----------|--------|-------------|
| `engine_set_modulation(source_int, target_int, intensity)` | **Removed** (Phase 15) | `engine_connect_ports(from_tag, from_port, to_tag, to_port)` |
| `VoiceFactory::createSH101()` (C++ internal) | **Removed** (Phase 15) | `engine_load_patch` or `engine_add_module` / `engine_connect_ports` / `engine_bake` |
| `engine_connect_mod(handle, src, voices, tgt, intensity)` | **Removed** (Phase 16) | `engine_connect_ports` — named port connections |
| `engine_create_processor(handle, type)` | **Removed** (Phase 16) | `engine_add_module(handle, type, tag)` |
| `engine_get_modulation_report(handle, buf, size)` | **Removed** (Phase 16) | No replacement; use named port connections and patch files. |
| `MOD_SRC_*` / `MOD_TGT_*` / `ALL_VOICES` constants | **Removed** (Phase 16) | Named port strings |
| `engine_set_lfo_rate(handle, hz)` | **Removed** (Phase 16) | `set_param(handle, "lfo_rate", hz)` after adding LFO as chain module |
| `engine_set_lfo_waveform(handle, waveform)` | **Removed** (Phase 16) | `set_param(handle, "lfo_waveform", v)` |
| `engine_set_lfo_depth(handle, target, depth)` | **Removed** (Phase 16) | `engine_connect_ports(handle, "LFO", "control_out", tag, port)` |
| `engine_set_lfo_intensity(handle, v)` | **Removed** (Phase 16) | `set_param(handle, "lfo_intensity", v)` |
| `engine_clear_modulations(handle)` | **Removed** (Phase 16) | No replacement; rebuild chain via `engine_add_module` / `engine_bake` |
