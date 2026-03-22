# Patch Format Specification (v2 / v3)

This document defines the JSON patch file format accepted by `engine_load_patch`,
`engine_load_patch_json`, and produced by `engine_get_patch_json` / `engine_save_patch`.
The authoritative patch examples are the files in `cxx/patches/`.

---

## Version 2 Format (baseline)

All patches use `"version": 2` as the baseline. The v1 format (integer-enum
`modulations` array) is no longer supported and must not be used in new patches.

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
| `version` | int | yes | `2` (baseline) or `3` (adds `post_chain`) |
| `name` | string | no | Human-readable patch name |
| `groups` | array | yes | One entry per voice group (typically one) |
| `post_chain` | array | no | **v3 only** — global post-summing effects; see §Post-Chain below |

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

Module type names (complete registry as of Phase 26):

**Generators / Oscillators:**
`COMPOSITE_GENERATOR`, `WHITE_NOISE`, `LFO`, `DRAWBAR_ORGAN`

**Envelopes:**
`ADSR_ENVELOPE`, `AD_ENVELOPE`

**Filters:**
`MOOG_FILTER`, `DIODE_FILTER`, `SH_FILTER`, `MS20_FILTER`, `HIGH_PASS_FILTER`, `BAND_PASS_FILTER`

**Dynamics:**
`VCA`, `NOISE_GATE`, `ENVELOPE_FOLLOWER`

**CV / Routing utilities:**
`INVERTER`, `CV_MIXER`, `CV_SPLITTER`, `CV_SCALER`, `MATHS`, `GATE_DELAY`, `SAMPLE_HOLD`,
`RING_MOD`, `AUDIO_SPLITTER`, `AUDIO_MIXER`

**FX:**
`ECHO_DELAY`, `PHASER`, `JUNO_CHORUS`, `DISTORTION`, `REVERB_FREEVERB`, `REVERB_FDN`

> `SOURCE_MIXER` is **not in the module registry** — multi-oscillator mixing is handled by `AUDIO_MIXER` (4 audio inputs, wirable) or by `COMPOSITE_GENERATOR`'s internal waveform mixer (parameter-controlled). Do not use `SOURCE_MIXER` in patch files.

> **Architecture note**: `REVERB_FREEVERB`, `REVERB_FDN`, `PHASER`, and `JUNO_CHORUS` are **global post-chain modules** — place them in the top-level `post_chain` array, not in the per-voice `chain`. `ECHO_DELAY` is timbral when used as a BBD shimmer before VCA (e.g. `cymbal.json`) and should stay in the voice chain; use `post_chain` for spacial send-echo usage (e.g. `delay_lead.json`). `DISTORTION` is a per-voice saturation effect and belongs in the voice `chain`.

See MODULE_DESC.md for port names, parameter ranges, and connection rules.

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

**Footage (VCO range selector)**: Roland System 100M oscillators have a range switch labelled in pipe-organ footage (2', 4', 8', 16', 32'). Use the `footage` parameter on `COMPOSITE_GENERATOR` to set the octave register. Omitting `footage` (or setting `footage: 8`) leaves the VCO at concert pitch.

| `footage` value | Semitone offset | Typical instrument |
|-----------------|-----------------|--------------------|
| `2`             | +24             | Piccolo, high leads |
| `4`             | +12             | Flute, high woodwinds |
| `8`             | 0               | Concert pitch (default) |
| `16`            | −12             | Cello, low brass |
| `32`            | −24             | Tuba, contrabass, sub-bass |

```json
"parameters": {
  "VCO": { "saw_gain": 1.0, "footage": 32 }
}
```

> `footage` and `transpose` write to the same internal semitone offset. Use `footage` when matching Roland documentation; use `transpose` for arbitrary interval tuning (e.g. bell partials, FM ratios). Do not set both in the same patch.

---

## Post-Chain Array (v3)

Global effects applied after all voices are summed, before the HAL output. A single instance processes the full stereo mix — not duplicated per voice.

```json
"post_chain": [
  { "type": "JUNO_CHORUS", "parameters": { "mode": 2, "rate": 0.5, "depth": 0.6 } },
  { "type": "REVERB_FDN",  "parameters": { "decay": 1.5, "wet": 0.25 } }
]
```

Effects run in the order listed. Each entry:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `type` | string | yes | Registered module type name |
| `parameters` | object | no | Initial parameter values (same format as group `parameters`) |

**Post-chain-appropriate module types** (global, not per-voice):
`JUNO_CHORUS`, `REVERB_FREEVERB`, `REVERB_FDN`, `PHASER`, `ECHO_DELAY` (send echo), `DISTORTION` (bus saturation)

Loading a v3 patch clears the existing post-chain before applying the file's `post_chain`. Loading a v2 patch (no `post_chain` key) also clears the post-chain.

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

Roland System 100M Fig 1-6 topology: VCO (saw) → VCF → VCA. ADSR fans via `CV_SPLITTER` to both VCA amplitude and (via `CV_SCALER`) VCF cutoff. The `CV_SCALER` `scale` parameter is the Roland "VCF MOD IN" depth knob — set it lower for subtle filter movement, higher (up to 1.0) for a more aggressive filter sweep. The characteristic brass "blat" comes from the fast attack (10ms) peaking then decaying to 65% sustain. See `patches/brass.json`.

```json
{
  "version": 3,
  "name": "Brass / Horn",
  "groups": [{
    "id": 0,
    "chain": [
      { "type": "COMPOSITE_GENERATOR", "tag": "VCO"  },
      { "type": "ADSR_ENVELOPE",       "tag": "ENV"  },
      { "type": "CV_SPLITTER",         "tag": "SPL"  },
      { "type": "CV_SCALER",           "tag": "VMOD" },
      { "type": "SH_FILTER",           "tag": "VCF"  },
      { "type": "VCA",                 "tag": "VCA"  }
    ],
    "connections": [
      { "from_tag": "VCO",  "from_port": "audio_out",    "to_tag": "VCF",  "to_port": "audio_in"  },
      { "from_tag": "VCF",  "from_port": "audio_out",    "to_tag": "VCA",  "to_port": "audio_in"  },
      { "from_tag": "ENV",  "from_port": "envelope_out", "to_tag": "SPL",  "to_port": "cv_in"     },
      { "from_tag": "SPL",  "from_port": "cv_out_1",     "to_tag": "VCA",  "to_port": "gain_cv"   },
      { "from_tag": "SPL",  "from_port": "cv_out_2",     "to_tag": "VMOD", "to_port": "cv_in"     },
      { "from_tag": "VMOD", "from_port": "cv_out",       "to_tag": "VCF",  "to_port": "cutoff_cv" }
    ],
    "parameters": {
      "VCO":  { "saw_gain": 1.0, "footage": 8 },
      "VCF":  { "cutoff": 2000.0, "resonance": 0.2 },
      "ENV":  { "attack": 0.01, "decay": 0.08, "sustain": 0.65, "release": 0.15 },
      "VMOD": { "scale": 0.5 }
    }
  }],
  "post_chain": [
    { "type": "REVERB_FREEVERB", "parameters": { "room_size": 0.55, "damping": 0.6, "wet": 0.2 } }
  ]
}
```

> `CV_SPLITTER` fans the single ADSR to both VCA and filter. `CV_SCALER` `scale=0.5` sets the filter modulation at 50% depth (the MOD IN knob at "5"). Trumpet uses `scale=0.9` (MOD IN "9") for a brighter, more aggressive filter sweep. Trombone also uses `scale=0.9` but with a sawtooth oscillator for warmer body. Tuba uses `footage=32` to transpose the VCO down two octaves into the sub-bass register.

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

## Advanced Patches

The patches in this section use modules implemented in Phases 17–19. All are fully buildable. See `cxx/patches/` for the authoritative JSON files. Patches still requiring future work are noted individually.

### Bell / Metallic

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

### Wind / Surf

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

> The S&H `cv_in` receives audio from VCO (noise channel). The `WHITE_NOISE` standalone module outputs `PORT_AUDIO`; `SAMPLE_HOLD` `cv_in` is `PORT_CONTROL`. In practice, use a standalone `WHITE_NOISE` node as chain head and wire its `audio_out` directly into `SAMPLE_HOLD` `cv_in` — since both are at audio rate, the port-type mismatch is acceptable as a documented patch authoring exception. Alternatively, route `COMPOSITE_GENERATOR` `noise_gain` at 1.0 and use VCO → SH directly.

### Harpsichord

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

### Pizzicato Violin

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

### Group Strings

Two detuned VCOs summed through an `AUDIO_MIXER` before a shared filter simulate an ensemble string section. `AUDIO_MIXER` accepts up to 4 audio inputs — use `audio_in_1` / `audio_in_2` for the two oscillators. See `patches/group_strings.json`.

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
        { "type": "AUDIO_MIXER",         "tag": "MIX"  },
        { "type": "MOOG_FILTER",         "tag": "VCF"  },
        { "type": "ADSR_ENVELOPE",       "tag": "ENV"  },
        { "type": "VCA",                 "tag": "VCA"  }
      ],
      "connections": [
        { "from_tag": "LFO1", "from_port": "control_out",  "to_tag": "VCO1", "to_port": "pitch_cv"   },
        { "from_tag": "LFO1", "from_port": "control_out",  "to_tag": "VCO2", "to_port": "pitch_cv"   },
        { "from_tag": "VCO1", "from_port": "audio_out",    "to_tag": "MIX",  "to_port": "audio_in_1" },
        { "from_tag": "VCO2", "from_port": "audio_out",    "to_tag": "MIX",  "to_port": "audio_in_2" },
        { "from_tag": "MIX",  "from_port": "audio_out",    "to_tag": "VCF",  "to_port": "audio_in"   },
        { "from_tag": "VCF",  "from_port": "audio_out",    "to_tag": "VCA",  "to_port": "audio_in"   },
        { "from_tag": "ENV",  "from_port": "envelope_out", "to_tag": "VCA",  "to_port": "gain_cv"    }
      ],
      "parameters": {
        "VCO1": { "saw_gain": 1.0 },
        "VCO2": { "saw_gain": 1.0, "detune": -12.0 },
        "MIX":  { "gain_1": 0.6, "gain_2": 0.6 },
        "VCF":  { "cutoff": 1800.0, "resonance": 0.12 },
        "ENV":  { "attack": 0.35, "decay": 0.0, "sustain": 1.0, "release": 0.5 },
        "LFO1": { "rate": 5.2, "intensity": 0.003 }
      }
    }
  ]
}
```

> `VCO2` `detune: -12.0` cents (slightly flat) produces ensemble beating against `VCO1`. `AUDIO_MIXER` sums the two oscillators before the filter, which both then share. `SOURCE_MIXER` is **not registered** — use `AUDIO_MIXER` for wired multi-VCO mixing.

### Banjo

Plucked twang: two detuned VCOs blended through a mixer, stripped of low weight by a HPF, then filtered by a resonant SH_FILTER for nasal membrane character. An AD envelope simultaneously drives VCA amplitude and VCF cutoff — the filter opens wide at the pluck then narrows as the note decays, producing the characteristic bright snap fading to warm ring. See `patches/banjo.json`.

```json
{
  "version": 2,
  "name": "Banjo",
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO1" },
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO2" },
        { "type": "AUDIO_MIXER",         "tag": "MIX"  },
        { "type": "HIGH_PASS_FILTER",    "tag": "HPF"  },
        { "type": "SH_FILTER",           "tag": "VCF"  },
        { "type": "AD_ENVELOPE",         "tag": "ENV"  },
        { "type": "VCA",                 "tag": "VCA"  }
      ],
      "connections": [
        { "from_tag": "VCO1", "from_port": "audio_out",    "to_tag": "MIX",  "to_port": "audio_in_1" },
        { "from_tag": "VCO2", "from_port": "audio_out",    "to_tag": "MIX",  "to_port": "audio_in_2" },
        { "from_tag": "MIX",  "from_port": "audio_out",    "to_tag": "HPF",  "to_port": "audio_in"   },
        { "from_tag": "HPF",  "from_port": "audio_out",    "to_tag": "VCF",  "to_port": "audio_in"   },
        { "from_tag": "VCF",  "from_port": "audio_out",    "to_tag": "VCA",  "to_port": "audio_in"   },
        { "from_tag": "ENV",  "from_port": "envelope_out", "to_tag": "VCA",  "to_port": "gain_cv"    },
        { "from_tag": "ENV",  "from_port": "envelope_out", "to_tag": "VCF",  "to_port": "cutoff_cv"  }
      ],
      "parameters": {
        "VCO1": { "saw_gain": 1.0 },
        "VCO2": { "pulse_gain": 0.7, "pulse_width": 0.2, "detune": 6 },
        "MIX":  { "gain_1": 1.0, "gain_2": 0.7 },
        "HPF":  { "cutoff": 250.0, "resonance": 0.1 },
        "VCF":  { "cutoff": 2500.0, "resonance": 0.5 },
        "ENV":  { "attack": 0.001, "decay": 0.20 }
      }
    }
  ]
}
```

> `VCO1` (sawtooth) provides the full harmonic series; `VCO2` (narrow pulse, 6 cents sharp) adds nasal colour and subtle beating. `HPF` at 250 Hz strips bass weight so the pluck sounds bright rather than thumpy. The dual `ENV → VCA + VCF` routing gives the characteristic filter-brightness-on-attack.

### Whistling / Pitch Glide

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

### Percussion Trill

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

### Cymbal

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
        "ENV":   { "attack": 0.001, "decay": 1.2, "sustain": 0.0, "release": 0.6 },
        "DLY":   { "time": 0.025, "feedback": 0.55, "mix": 0.5, "mod_rate": 6.5, "mod_intensity": 0.4 }
      }
    }
  ]
}
```

> The `feedback: true` connection re-enters the delay's own input. `DLY` `mod_rate: 6.5` Hz with `mod_intensity: 0.4` oscillates the 25 ms delay time by ±10 ms, producing the metallic shimmer. `VCF` `hpf_cutoff: 2` selects the ≈150 Hz high-pass stage to remove low-frequency mud. `ENV` `decay: 1.2` and `release: 0.6` give a realistic sustained shimmer decay — shorter values produce a tighter, dryer hit. See `patches/cymbal.json`.

### Acid Reverb (TB-303 with Distortion)

TB-303 style acid bass with authentic two-envelope architecture. On the real 303 the Decay knob controls **only the filter sweep** — the VCA is gated (open while key held, close on release) and is not shaped by Decay at all. Two separate envelopes replicate this:

- `AD_ENVELOPE` (ENV) — variable decay sweeps the filter cutoff only
- `ADSR_ENVELOPE` (GATE) — sustain=1.0, release≈20ms acts as a shaped gate for the VCA

Filter state is preserved across notes (no reset on note_on), so resonance builds across the riff. `DISTORTION` is post-VCA, replicating the 303 output plugged into a pedal. `REVERB_FDN` is in `post_chain` (global bus, not per-voice). See `patches/acid_reverb.json`.

```json
{
  "version": 3,
  "name": "Acid Reverb",
  "groups": [
    {
      "id": 0,
      "chain": [
        { "type": "COMPOSITE_GENERATOR", "tag": "VCO"  },
        { "type": "DIODE_FILTER",        "tag": "VCF"  },
        { "type": "AD_ENVELOPE",         "tag": "ENV"  },
        { "type": "ADSR_ENVELOPE",       "tag": "GATE" },
        { "type": "VCA",                 "tag": "VCA"  },
        { "type": "DISTORTION",          "tag": "DIST" }
      ],
      "connections": [
        { "from_tag": "VCO",  "from_port": "audio_out",    "to_tag": "VCF",  "to_port": "audio_in"  },
        { "from_tag": "VCF",  "from_port": "audio_out",    "to_tag": "VCA",  "to_port": "audio_in"  },
        { "from_tag": "GATE", "from_port": "envelope_out", "to_tag": "VCA",  "to_port": "gain_cv"   },
        { "from_tag": "ENV",  "from_port": "envelope_out", "to_tag": "VCF",  "to_port": "cutoff_cv" },
        { "from_tag": "VCA",  "from_port": "audio_out",    "to_tag": "DIST", "to_port": "audio_in"  }
      ],
      "parameters": {
        "VCO":  { "saw_gain": 1.0 },
        "VCF":  { "cutoff": 800.0, "resonance": 0.92, "env_depth": 3.0 },
        "DIST": { "drive": 8.0, "character": 0.3 },
        "ENV":  { "attack": 0.003, "decay": 0.25 },
        "GATE": { "attack": 0.003, "decay": 0.0, "sustain": 1.0, "release": 0.020 }
      }
    }
  ],
  "post_chain": [
    { "type": "REVERB_FDN", "parameters": { "decay": 1.2, "wet": 0.20, "room_size": 0.5, "damping": 0.4 } }
  ]
}
```

> `VCF` `resonance: 0.92` is on the edge of self-oscillation — the 303's characteristic squelch. `env_depth: 3.0` scales the AD envelope to sweep one octave of filter cutoff. `GATE` ADSR with `sustain=1.0` and `release=0.020` acts as a hard gate: VCA opens on note_on and closes cleanly 20ms after note_off, independent of the filter's decay time. This matches the authentic 303 behaviour where the Decay knob has no effect on VCA amplitude. `DIST` `character: 0.3` gives mild even-harmonic asymmetry without going full ring distortion. See MODULE_DESC.md §DIODE_FILTER for the TB-303 two-envelope pattern.

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

This format is **not supported** in Phase 15 and later. The integer enums (`MOD_SRC_LFO`, `MOD_TGT_PULSEWIDTH`, etc.), `engine_connect_mod`, and `engine_set_lfo_*` are all removed (Phase 15–16). LFO is a first-class chain module; see BRIDGE_GUIDE.md §7.
