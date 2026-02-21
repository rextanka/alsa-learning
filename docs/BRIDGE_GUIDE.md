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
engine_note_on_panned(engine, 64, 0.8f, -0.5f);

// Change pan position of an active note
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

## 4. Bridge Objects (Handles)

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
