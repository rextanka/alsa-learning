# SH-101 Bass Patch Specification (Phase 13)

This document defines the JSON structure for the SH-101 Bass patch used in `Functional_SH101_Live`.

## Patch Definition (`sh101_bass.json`)

```json
{
    "version": 1,
    "name": "sh101_bass",
    "parameters": {
        "pulse_gain": 0.8,
        "sub_gain": 0.6,
        "noise_gain": 0.1,
        "vcf_cutoff": 450.0,
        "vcf_res": 0.6,
        "attack": 0.005,
        "decay": 0.15,
        "sustain": 0.4,
        "release": 0.1
    },
    "modulations": [
        {
            "source": 4,
            "target": 1,
            "intensity": 0.1
        },
        {
            "source": 3,
            "target": 2,
            "intensity": 1.5
        }
    ]
}
```

## Parameter Mappings

| JSON Key | Target Component | Description |
|----------|------------------|-------------|
| `pulse_gain` | SourceMixer (Ch 1) | Gain for the main Pulse/Square oscillator. |
| `sub_gain` | SourceMixer (Ch 2) | Gain for the phase-locked Sub-Oscillator. |
| `noise_gain` | SourceMixer (Ch 3) | Gain for the white noise source. |
| `vcf_cutoff` | Moog/Diode Filter | Base cutoff frequency in Hz. |
| `vcf_res` | Moog/Diode Filter | Filter resonance (0.0 to 1.0). |
| `attack` | ADSR Envelope | Attack time in seconds (90/10 pluck target: 0.005s). |
| `decay` | ADSR Envelope | Decay time in seconds (90/10 pluck target: 0.15s). |
| `sustain` | ADSR Envelope | Sustain level (0.0 to 1.0). |
| `release` | ADSR Envelope | Release time in seconds. |

## Modulation Matrix Enums

### Sources
- `3`: `MOD_SRC_ENVELOPE`
- `4`: `MOD_SRC_LFO`

### Targets
- `1`: `MOD_TGT_PULSEWIDTH` (PWM)
- `2`: `MOD_TGT_CUTOFF` (Filter Modulation)
- `3`: `MOD_TGT_AMPLITUDE` (VCA - Defaulted to 1.0 in code if not specified)

## Technical Notes
- **VCA Logic**: The Envelope is multiplied by the Amplitude target in `Voice::do_pull`.
- **Saturation**: Oscillators are scaled by `0.707f` before the `tanh` stage in `SourceMixer` to maintain headroom.
- **Phase Lock**: The Sub-Oscillator tracks the phase of the `PulseOscillatorProcessor` for drift-free alignment.
