# Patch Format Specification (v2)

This document defines the JSON patch file format used by `engine_load_patch`. The
authoritative patch examples are the four reference files in `cxx/patches/`.

---

## Version 2 Format

All current patches use `"version": 2`. The v1 format (integer-enum `modulations` array)
is no longer supported and must not be used in new patches.

### Top-Level Structure

```json
{
  "version": 2,
  "name": "sh_bass",
  "groups": [
    { ... }
  ]
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `version` | int | yes | Must be `2` |
| `name` | string | no | Human-readable patch name |
| `groups` | array | yes | One entry per voice group (typically one) |

---

## Group Object

```json
{
  "id": 0,
  "chain": [ ... ],
  "connections": [ ... ],
  "parameters": { ... }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `id` | int | Voice group index (0-based) |
| `chain` | array | Ordered list of modules — **generator must be first** |
| `connections` | array | Named port connections wired after `chain` construction |
| `parameters` | object | Initial parameter values by string label |

---

## Chain Entry

```json
{ "type": "COMPOSITE_GENERATOR", "tag": "VCO" }
```

| Field | Description |
|-------|-------------|
| `type` | Registered module type name (see MODULE_DESC.md) |
| `tag` | Unique instance tag for this group — used to target ports and parameters |

Module type names: `COMPOSITE_GENERATOR`, `ADSR_ENVELOPE`, `VCA`, `MOOG_FILTER`,
`DIODE_FILTER`, `LFO`, `DRAWBAR_ORGAN`, `WHITE_NOISE`, `INVERTER`, `RING_MOD`,
`SOURCE_MIXER`, `SAMPLE_HOLD`, `CV_MIXER`, `CV_SPLITTER`, `AUDIO_SPLITTER`,
`GATE_DELAY`, `PHASE_SHIFTER`, `ECHO_DELAY`, `REVERB`, `NOISE_GATE`. See MODULE_DESC.md for the full registry.

---

## Connection Object

```json
{ "from_tag": "ENV", "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv" }
```

| Field | Description |
|-------|-------------|
| `from_tag` | Tag of the source module |
| `from_port` | Output port name on the source (see MODULE_DESC.md) |
| `to_tag` | Tag of the destination module |
| `to_port` | Input port name on the destination |

For feedback connections (e.g. delay feedback) add `"feedback": true` to break the cycle:

```json
{ "from_tag": "DLY", "from_port": "audio_out", "to_tag": "DLY", "to_port": "audio_in", "feedback": true }
```

Port type rules enforced at load time:
- `PORT_AUDIO` out → `PORT_AUDIO` in only
- `PORT_CONTROL` out → `PORT_CONTROL` in only

---

## Parameters Object

Nested object keyed by instance tag, then parameter name. Every node in the patch that needs non-default values gets its own sub-object.

```json
"parameters": {
  "VCO": {
    "sine_gain": 1.0
  },
  "ENV": {
    "attack":  0.005,
    "decay":   0.15,
    "sustain": 0.4,
    "release": 0.1
  },
  "VCF": {
    "cutoff": 450.0,
    "res":    0.6
  }
}
```

The patch loader maps each entry as `voice.find_by_tag(tag)->apply_parameter(name, value)`. Tags not present in the `parameters` object receive module defaults. Unknown parameter names are silently ignored with a warning log.

**Multiple oscillators**: each VCO instance is addressed independently by its tag:

```json
"parameters": {
  "VCO1": { "saw_gain": 1.0 },
  "VCO2": { "saw_gain": 1.0, "detune": -7.0 }
}
```

---

## Complete Example — SH-101 Bass

```json
{
  "version": 2,
  "name": "sh_bass",
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO" },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV" },
        { "type": "VCA",                 "tag": "VCA" }
      ],
      "connections": [
        { "from_tag": "ENV", "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv" }
      ],
      "parameters": {
        "VCO": {
          "pulse_gain": 0.8,
          "sub_gain":   0.6,
          "noise_gain": 0.1
        },
        "ENV": {
          "attack":  0.005,
          "decay":   0.15,
          "sustain": 0.4,
          "release": 0.1
        }
      }
    }
  ]
}
```

---

## Patch Library — Roland System 100M Style Patches

The patches below are derived from Roland's *Practical Synthesis for Electronic Music, Volumes 1 and 2* (Roland, 2nd Ed.). They use only currently-implemented modules unless otherwise noted.

### Violin (Direct Vibrato)

Bowed string character: slow attack, LFO vibrato applied continuously from note onset. For true *delayed* vibrato (vibrato ramps in after the bow takes hold) a `CV_MIXER` + second `ADSR_ENVELOPE` are required — see the Gaps table in MODULE_DESC.md.

```json
{
  "version": 2,
  "name": "violin_vibrato",
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "LFO",                 "tag": "LFO1" },
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO"  },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV"  },
        { "type": "VCA",                 "tag": "VCA"  }
      ],
      "connections": [
        { "from_tag": "LFO1", "from_port": "control_out",  "to_tag": "VCO", "to_port": "pitch_cv" },
        { "from_tag": "ENV",  "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv"  }
      ],
      "parameters": {
        "VCO":  { "saw_gain": 1.0, "sub_gain": 0.2 },
        "ENV":  { "attack": 0.3, "decay": 0.0, "sustain": 1.0, "release": 0.4 },
        "VCF":  { "cutoff": 1600.0, "res": 0.1 },
        "LFO1": { "rate": 5.5, "intensity": 0.003 }
      }
    }
  ]
}
```

### Brass / Horn

Bright, fast-attack character. The filter is set wide-open and a short amplitude decay gives the characteristic blatt envelope. For a full brass patch with a separate filter-sweep envelope a second `ADSR_ENVELOPE` instance and per-tag parameter addressing are required.

```json
{
  "version": 2,
  "name": "brass",
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO" },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV" },
        { "type": "VCA",                 "tag": "VCA" }
      ],
      "connections": [
        { "from_tag": "ENV", "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv" }
      ],
      "parameters": {
        "VCO": { "saw_gain": 1.0, "pulse_gain": 0.3 },
        "ENV": { "attack": 0.01, "decay": 0.08, "sustain": 0.65, "release": 0.15 },
        "VCF": { "cutoff": 3200.0, "res": 0.28 }
      }
    }
  ]
}
```

### Flute

Breathy sine-dominant tone with a small noise contribution simulating breath turbulence. Gentle LFO vibrato. Medium attack approximates the initial air column build-up.

```json
{
  "version": 2,
  "name": "flute",
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "LFO",                 "tag": "LFO1" },
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO"  },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV"  },
        { "type": "VCA",                 "tag": "VCA"  }
      ],
      "connections": [
        { "from_tag": "LFO1", "from_port": "control_out",  "to_tag": "VCO", "to_port": "pitch_cv" },
        { "from_tag": "ENV",  "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv"  }
      ],
      "parameters": {
        "VCO":  { "sine_gain": 1.0, "triangle_gain": 0.15, "noise_gain": 0.12 },
        "ENV":  { "attack": 0.1, "decay": 0.05, "sustain": 0.85, "release": 0.25 },
        "VCF":  { "cutoff": 5000.0, "res": 0.05 },
        "LFO1": { "rate": 5.0, "intensity": 0.002 }
      }
    }
  ]
}
```

### Bowed Bass

Deep bowed-string character using a diode ladder filter for added harmonic grit. Triangle/sawtooth blend, slow attack, medium-low cutoff.

```json
{
  "version": 2,
  "name": "bowed_bass",
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO" },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV" },
        { "type": "VCA",                 "tag": "VCA" }
      ],
      "connections": [
        { "from_tag": "ENV", "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv" }
      ],
      "parameters": {
        "VCO": { "saw_gain": 0.6, "triangle_gain": 0.8, "sub_gain": 0.3 },
        "ENV": { "attack": 0.25, "decay": 0.0, "sustain": 1.0, "release": 0.5 },
        "VCF": { "cutoff": 420.0, "res": 0.38, "filter_type": 1 }
      }
    }
  ]
}
```

> `"filter_type": 1` under `"VCF"` selects `DIODE_FILTER`. This parameter is currently applied via `Voice::set_filter_type(int)` — it is not yet a chain-level module selection.

### Dog Whistle

Rising pitch sweep on key press followed by an immediate drop back to the base pitch. A slow-attack ADSR drives the VCO `pitch_cv`, sweeping the frequency up by up to one octave (1.0 = +1 oct in 1V/oct) during the attack window; with zero sustain and zero decay the pitch drops back instantly when the attack peak is reached. A separate fast-attack amplitude ADSR holds the note at normal level while the pitch sweeps. (Roland Vol 1 §2-2, Fig 2-6.)

```json
{
  "version": 2,
  "name": "dog_whistle",
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO"       },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV_PITCH" },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV_AMP"   },
        { "type": "VCA",                 "tag": "VCA"       }
      ],
      "connections": [
        { "from_tag": "ENV_PITCH", "from_port": "envelope_out", "to_tag": "VCO", "to_port": "pitch_cv" },
        { "from_tag": "ENV_AMP",   "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv"  }
      ],
      "parameters": {
        "VCO":       { "sine_gain": 1.0 },
        "ENV_PITCH": { "attack": 0.18, "decay": 0.0, "sustain": 0.0, "release": 0.04 },
        "ENV_AMP":   { "attack": 0.01, "decay": 0.0, "sustain": 1.0, "release": 0.12 }
      }
    }
  ]
}
```

> `ENV_PITCH` `sustain: 0.0` with no decay means the pitch reaches peak at the end of the 180 ms attack window then instantly drops back to the base pitch. The 40 ms release provides a brief glide-back rather than a hard cut. `ENV_AMP` keeps amplitude steady throughout.

### Percussion Noise

Short-decay percussive noise hit. White noise through the implicit filter with a fast-attack, fast-decay ADSR. No VCO needed — `WHITE_NOISE` is the chain head. Suits snare-adjacent hits, hand percussion, and click attacks.

```json
{
  "version": 2,
  "name": "percussion_noise",
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "WHITE_NOISE",    "tag": "NOISE" },
        { "type": "ADSR_ENVELOPE",  "tag": "ENV"   },
        { "type": "VCA",            "tag": "VCA"   }
      ],
      "connections": [
        { "from_tag": "ENV", "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv" }
      ],
      "parameters": {
        "ENV": { "attack": 0.001, "decay": 0.18, "sustain": 0.0, "release": 0.08 },
        "VCF": { "cutoff": 3500.0, "res": 0.45 }
      }
    }
  ]
}
```

---

## Patches Requiring Planned Modules

The topologies below require modules that are not yet implemented. They are documented here so the patch format and wiring intent are established ahead of implementation.

### Bell / Metallic (requires `RING_MOD`)

Two VCOs at an inharmonic interval (ratio ≈ 2.756) fed into a ring modulator produce bell partials. A fast-decay percussive envelope shapes the amplitude.

```json
{
  "version": 2,
  "name": "bell",
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO1" },
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO2" },
        { "type": "RING_MOD",            "tag": "RM"   },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV"  },
        { "type": "VCA",                 "tag": "VCA"  }
      ],
      "connections": [
        { "from_tag": "VCO1", "from_port": "audio_out",    "to_tag": "RM",  "to_port": "audio_in_a" },
        { "from_tag": "VCO2", "from_port": "audio_out",    "to_tag": "RM",  "to_port": "audio_in_b" },
        { "from_tag": "RM",   "from_port": "audio_out",    "to_tag": "VCA", "to_port": "audio_in"   },
        { "from_tag": "ENV",  "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv"    }
      ],
      "parameters": {
        "VCO1": { "sine_gain": 1.0 },
        "VCO2": { "sine_gain": 1.0, "transpose": 19, "detune": 63 },
        "ENV":  { "attack": 0.001, "decay": 1.2, "sustain": 0.0, "release": 0.8 }
      }
    }
  ]
}
```

> `VCO2` `transpose: 19` semitones + `detune: 63` cents ≈ +1663 cents total, giving the ≈ 2.756× inharmonic ratio for bell partials. Both `transpose` and `detune` are now declared parameters on `COMPOSITE_GENERATOR`.

### Wind / Surf (requires `SAMPLE_HOLD`)

Pink noise (or white noise) sampled by a slow LFO clock to produce slowly drifting pitch-like color variation through the filter.

```json
{
  "version": 2,
  "name": "wind_surf",
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "LFO",                 "tag": "LFO_CLK" },
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO"     },
        { "type": "SAMPLE_HOLD",         "tag": "SH"      },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV"     },
        { "type": "VCA",                 "tag": "VCA"     }
      ],
      "connections": [
        { "from_tag": "VCO",     "from_port": "audio_out",    "to_tag": "SH",  "to_port": "cv_in"    },
        { "from_tag": "LFO_CLK","from_port": "control_out",  "to_tag": "SH",  "to_port": "clock_in" },
        { "from_tag": "SH",     "from_port": "cv_out",       "to_tag": "VCO", "to_port": "pitch_cv" },
        { "from_tag": "ENV",    "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv"  }
      ],
      "parameters": {
        "VCO":     { "noise_gain": 1.0 },
        "ENV":     { "attack": 0.5, "decay": 0.0, "sustain": 1.0, "release": 1.0 },
        "VCF":     { "cutoff": 800.0, "res": 0.05 },
        "LFO_CLK": { "rate": 1.2 }
      }
    }
  ]
}
```

> The S&H `cv_in` above receives audio from VCO (noise channel). Port type enforcement note: `audio_out` is `PORT_AUDIO` and `cv_in` is `PORT_CONTROL` — this wiring will require either a dedicated noise CV output on `COMPOSITE_GENERATOR` or a dedicated `WHITE_NOISE` node with a `PORT_CONTROL` output variant.

### Harpsichord (requires `INVERTER`)

Plucked-string character: fast pulse-wave VCO, percussive ADSR with zero sustain. The inverted ADSR simultaneously sweeps the filter cutoff in the opposite direction to the amplitude — the filter brightens as the note decays, producing the characteristic harpsichord twang. Requires the `INVERTER` module and filter `cutoff_cv` connections (filter-as-chain-node refactor).

```json
{
  "version": 2,
  "name": "harpsichord",
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO"  },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV"  },
        { "type": "INVERTER",            "tag": "INV"  },
        { "type": "VCA",                 "tag": "VCA"  }
      ],
      "connections": [
        { "from_tag": "ENV", "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv"   },
        { "from_tag": "ENV", "from_port": "envelope_out", "to_tag": "INV", "to_port": "cv_in"     },
        { "from_tag": "INV", "from_port": "cv_out",       "to_tag": "VCF", "to_port": "cutoff_cv" }
      ],
      "parameters": {
        "VCO": { "pulse_gain": 1.0, "pulse_width": 0.35 },
        "ENV": { "attack": 0.001, "decay": 0.5, "sustain": 0.0, "release": 0.1 },
        "INV": { "scale": -0.6 },
        "VCF": { "cutoff": 1200.0, "res": 0.3 }
      }
    }
  ]
}
```

> `INV` `scale: -0.6` attenuates the inverted sweep so the filter doesn't close completely at peak amplitude. The base `VCF` `cutoff` is the resting (post-decay) brightness; during the attack the inverted ADSR pulls cutoff down by up to 60% of envelope depth.

### Pizzicato Violin (requires filter-as-chain-node refactor)

Plucked string character. A single ADSR drives VCA amplitude (fast attack, medium decay, zero sustain) while a second ADSR with a shorter decay drives VCF cutoff — the filter sweeps down as the amplitude decays, producing the characteristic pizzicato bite-then-mellow. Requires `MOOG_FILTER` as a chain node with a wirable `cutoff_cv` input.

```json
{
  "version": 2,
  "name": "pizzicato_violin",
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO"  },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV1" },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV2" },
        { "type": "MOOG_FILTER",         "tag": "VCF"  },
        { "type": "VCA",                 "tag": "VCA"  }
      ],
      "connections": [
        { "from_tag": "ENV1", "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv"   },
        { "from_tag": "ENV2", "from_port": "envelope_out", "to_tag": "VCF", "to_port": "cutoff_cv" }
      ],
      "parameters": {
        "VCO":  { "saw_gain": 0.8, "triangle_gain": 0.4 },
        "ENV1": { "attack": 0.002, "decay": 0.35, "sustain": 0.0, "release": 0.15 },
        "ENV2": { "attack": 0.001, "decay": 0.2,  "sustain": 0.0, "release": 0.1  },
        "VCF":  { "cutoff": 2800.0, "res": 0.2 }
      }
    }
  ]
}
```

### Group Strings (requires SOURCE_MIXER chain wiring)

Two detuned VCOs summed before a shared filter simulate an ensemble string section. VCO1 is the main voice; VCO2 is tuned slightly flat to produce the characteristic chorus/ensemble beating. Requires `SOURCE_MIXER` as a wired chain executor node rather than just embedded inside `COMPOSITE_GENERATOR`.

```json
{
  "version": 2,
  "name": "group_strings",
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "LFO",                 "tag": "LFO1" },
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO1" },
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO2" },
        { "type": "SOURCE_MIXER",        "tag": "MIX"  },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV"  },
        { "type": "VCA",                 "tag": "VCA"  }
      ],
      "connections": [
        { "from_tag": "LFO1", "from_port": "control_out",  "to_tag": "VCO1", "to_port": "pitch_cv"   },
        { "from_tag": "LFO1", "from_port": "control_out",  "to_tag": "VCO2", "to_port": "pitch_cv"   },
        { "from_tag": "VCO1", "from_port": "audio_out",    "to_tag": "MIX",  "to_port": "audio_in_1" },
        { "from_tag": "VCO2", "from_port": "audio_out",    "to_tag": "MIX",  "to_port": "audio_in_2" },
        { "from_tag": "ENV",  "from_port": "envelope_out", "to_tag": "VCA",  "to_port": "gain_cv"    }
      ],
      "parameters": {
        "VCO1": { "saw_gain": 1.0 },
        "VCO2": { "saw_gain": 1.0, "detune": -12.0 },
        "ENV":  { "attack": 0.35, "decay": 0.0, "sustain": 1.0, "release": 0.5 },
        "VCF":  { "cutoff": 1800.0, "res": 0.12 },
        "LFO1": { "rate": 5.2, "intensity": 0.003, "delay": 2.0 },
        "MIX":  { "gain_1": 0.6, "gain_2": 0.6 }
      }
    }
  ]
}
```

> `LFO1` `delay: 2.0` uses the new LFO `delay` parameter — vibrato ramps in 2 seconds after note onset. `VCO2` `detune: -12.0` cents (≈ half a semitone flat) produces ensemble beating against VCO1.

### Banjo (requires `ext_gate_in` on ADSR)

Plucked, self-retriggering twang. A fast-decay pulse-wave VCO with zero sustain is re-triggered at a fixed rate by an LFO square wave feeding `ext_gate_in`, producing a continuous picked-string roll while the key is held. Similar topology to Percussion Trill but at a lower rate with a brighter, more pitched character. (Roland Vol 1 §3-4, Fig 3-4.)

```json
{
  "version": 2,
  "name": "banjo",
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "LFO",                 "tag": "PICK" },
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO"  },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV"  },
        { "type": "VCA",                 "tag": "VCA"  }
      ],
      "connections": [
        { "from_tag": "PICK", "from_port": "control_out",  "to_tag": "ENV", "to_port": "ext_gate_in" },
        { "from_tag": "ENV",  "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv"     }
      ],
      "parameters": {
        "VCO":  { "pulse_gain": 0.9, "pulse_width": 0.45 },
        "ENV":  { "attack": 0.002, "decay": 0.12, "sustain": 0.0, "release": 0.05 },
        "VCF":  { "cutoff": 2200.0, "res": 0.2 },
        "PICK": { "rate": 6.5, "waveform": 2 }
      }
    }
  ]
}
```

> `PICK` `waveform: 2` is Square. At `rate: 6.5` Hz this produces approximately 6–7 picks per second. Adjust `PICK` `rate` and `ENV` `decay` together to control the speed and overlap of the picking pattern.

### Whistling / Pitch Glide (requires `INVERTER`)

A pitch that bends up from the base frequency on attack (human-whistle character) with added LFO vibrato. A second `ADSR_ENVELOPE` drives `pitch_cv` through an `INVERTER` to produce an upward bend on attack (inverted decay sweep). The primary `ADSR_ENVELOPE` shapes amplitude with a slow attack. A slow LFO adds vibrato once the note is established. (Roland Vol 1 §3-5/3-6, Figs 3-5–3-6.)

```json
{
  "version": 2,
  "name": "whistling_glide",
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "LFO",                 "tag": "LFO1"      },
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO"       },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV_AMP"   },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV_PITCH" },
        { "type": "INVERTER",            "tag": "INV"       },
        { "type": "VCA",                 "tag": "VCA"       }
      ],
      "connections": [
        { "from_tag": "ENV_PITCH", "from_port": "envelope_out", "to_tag": "INV",  "to_port": "cv_in"   },
        { "from_tag": "INV",       "from_port": "cv_out",       "to_tag": "VCO",  "to_port": "pitch_cv" },
        { "from_tag": "LFO1",      "from_port": "control_out",  "to_tag": "VCO",  "to_port": "pitch_cv" },
        { "from_tag": "ENV_AMP",   "from_port": "envelope_out", "to_tag": "VCA",  "to_port": "gain_cv"  }
      ],
      "parameters": {
        "VCO":       { "sine_gain": 1.0 },
        "ENV_AMP":   { "attack": 0.12, "decay": 0.0,  "sustain": 1.0, "release": 0.2  },
        "ENV_PITCH": { "attack": 0.0,  "decay": 0.25, "sustain": 0.0, "release": 0.0  },
        "INV":       { "scale": -0.4 },
        "VCF":       { "cutoff": 8000.0, "res": 0.05 },
        "LFO1":      { "rate": 5.8, "intensity": 0.002, "delay": 1.5 }
      }
    }
  ]
}
```

> `ENV_PITCH` decays rapidly to zero, producing a downward pitch movement that `INV` `scale: -0.4` flips to an upward bend of 0.4 octave. `LFO1` `delay: 1.5` allows the bend to settle before vibrato begins. Both `LFO1` and `INV` feed `VCO` `pitch_cv` simultaneously — the pitch CV inputs are summed at the port. Requires `INVERTER` (planned).

### Percussion Trill (requires `ext_gate_in` on ADSR)

LFO square wave re-triggers the envelope continuously while a key is held, producing a rapid trill effect. The `ext_gate_in` port on `ADSR_ENVELOPE` is OR'd with the lifecycle `gate_in` so both sources can trigger it.

```json
{
  "version": 2,
  "name": "percussion_trill",
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "LFO",                 "tag": "TRILL" },
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO"   },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV"   },
        { "type": "VCA",                 "tag": "VCA"   }
      ],
      "connections": [
        { "from_tag": "TRILL", "from_port": "control_out",  "to_tag": "ENV", "to_port": "ext_gate_in" },
        { "from_tag": "ENV",   "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv"     }
      ],
      "parameters": {
        "VCO":   { "pulse_gain": 1.0, "pulse_width": 0.4 },
        "ENV":   { "attack": 0.005, "decay": 0.06, "sustain": 0.0, "release": 0.03 },
        "VCF":   { "cutoff": 1400.0, "res": 0.25 },
        "TRILL": { "rate": 8.0, "waveform": 2 }
      }
    }
  ]
}
```

> `TRILL` `waveform: 2` is the Square waveform. At `rate: 8.0` this produces 8 retriggered hits per second.

### Cymbal (requires `ECHO_DELAY` with modulation, filter-as-chain-node)

Metallic cymbal character: high-pass filtered white noise into a modulated short delay for shimmer, shaped by a percussive ADSR. The `ECHO_DELAY` `mod_rate` and `mod_intensity` parameters wobble the delay time, producing the characteristic inharmonic shimmer of a struck cymbal. Requires `ECHO_DELAY` (planned) and `MOOG_FILTER` or `DIODE_FILTER` as a chain node. (Roland Vol 2 §3-5, Fig 3-16.)

```json
{
  "version": 2,
  "name": "cymbal",
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "WHITE_NOISE",   "tag": "NOISE" },
        { "type": "MOOG_FILTER",   "tag": "VCF"   },
        { "type": "ADSR_ENVELOPE", "tag": "ENV"   },
        { "type": "ECHO_DELAY",    "tag": "DLY"   },
        { "type": "VCA",           "tag": "VCA"   }
      ],
      "connections": [
        { "from_tag": "NOISE", "from_port": "audio_out",    "to_tag": "VCF", "to_port": "audio_in" },
        { "from_tag": "VCF",   "from_port": "audio_out",    "to_tag": "DLY", "to_port": "audio_in" },
        { "from_tag": "DLY",   "from_port": "audio_out",    "to_tag": "VCA", "to_port": "audio_in" },
        { "from_tag": "ENV",   "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv"  },
        { "from_tag": "DLY",   "from_port": "audio_out",    "to_tag": "DLY", "to_port": "audio_in", "feedback": true }
      ],
      "parameters": {
        "NOISE": { "color": 0 },
        "VCF":   { "cutoff": 6000.0, "resonance": 0.35, "hpf_cutoff": 2 },
        "ENV":   { "attack": 0.001, "decay": 0.35, "sustain": 0.0, "release": 0.15 },
        "DLY":   { "time": 0.025, "feedback": 0.55, "mix": 0.5, "mod_rate": 6.5, "mod_intensity": 0.4 }
      }
    }
  ]
}
```

> The `feedback: true` connection re-enters the delay's own input. `DLY` `mod_rate: 6.5` Hz with `mod_intensity: 0.4` oscillates the 25 ms delay time by ±10 ms, producing the metallic shimmer. `VCF` `hpf_cutoff: 2` selects the ≈150 Hz high-pass stage to remove low-frequency mud. The `VCF` here requires filter-as-chain-node refactor to accept wired `audio_in`.

---

## Topology Reuse on Patch Load

If `engine_load_patch` detects that the incoming patch's chain (module types and tags in
the same order) matches the currently active chain for a group, it skips teardown and only
applies the parameter and connection updates. This avoids audio interruption when switching
between patches that share a topology.

---

## Legacy v1 Format (Archived)

The v1 format used integer-enum `modulations` to wire sources to targets:

```json
{ "version": 1, "parameters": { ... }, "modulations": [{ "source": 4, "target": 1, "intensity": 0.1 }] }
```

This format is **not supported** in Phase 15 and later, and patch files must use v2. The
integer enums (`MOD_SRC_LFO`, `MOD_TGT_PULSEWIDTH`, etc.) and `engine_connect_mod` are
deprecated in Phase 15A and will be removed in Phase 16. Runtime LFO routing now uses
the `engine_set_lfo_*` API (see BRIDGE_GUIDE.md §7).
