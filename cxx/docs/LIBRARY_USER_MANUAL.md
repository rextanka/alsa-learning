# DSP Audio Engine — Library User Manual

This manual explains how to build synthesizer patches and signal chains with this DSP library.
Synthesis concepts are cross-referenced to Roland's *A Foundation for Electronic Music* (2nd Ed.,
R. D. Graham, 1978), *Practical Synthesis for Electronic Music, Vol. 1* (R. D. Graham, 1979),
and *Practical Synthesis for Electronic Music, Vol. 2* (2nd Ed., R. D. Graham) where applicable.

---

## 1  The Dimensions of Musical Sound

A single sustained tone has three purely acoustic properties — pitch, timbre, and loudness.
Music requires a fourth dimension: **temporality**, the organization of sound in time.
This library models all four:

| Dimension   | What it governs                                             | Primary module family |
|-------------|-------------------------------------------------------------|-----------------------|
| Pitch       | Frequency and harmonic content of the fundamental          | `COMPOSITE_GENERATOR`, `DRAWBAR_ORGAN`, `WHITE_NOISE` |
| Timbre      | Spectral shape — which overtones reach the listener        | `MOOG_FILTER`, `DIODE_FILTER`, `SH_FILTER`, `MS20_FILTER`, `HIGH_PASS_FILTER`, `BAND_PASS_FILTER` |
| Dynamics    | Amplitude level and how it evolves within a note           | `VCA`, `ADSR_ENVELOPE`, `AD_ENVELOPE` |
| Temporality | Time structure — tempo, meter, beat-relative timing, modulation rate | `LFO`, `ECHO_DELAY`, `PHASER`, engine transport (`MusicalClock`) |

The core design — a chain of `Processor` nodes pulled in order — directly mirrors the
VCO → VCF → VCA signal flow diagrams in the Roland synthesis texts.

### Envelopes span dynamics and temporality

`ADSR_ENVELOPE` and `AD_ENVELOPE` occupy both dimensions simultaneously: their *level*
parameters (peak level, sustain level) are dynamic — they determine how loud; their *timing*
parameters (attack time, decay time, release time) are temporal — they determine for how long.
This is why envelopes are the most musically expressive single module type.

### Two layers of temporality

**Local temporal evolution** governs how a single note changes within its lifetime: an envelope
fades in and out, an LFO cycles through a vibrato wave, an echo trail rings out. These processors
are self-contained — they require no external clock.

**Global temporal structure** governs where notes fall in musical time: tempo (beats per minute),
meter (beats per bar), and beat position. The engine's `MusicalClock` is the authoritative source
for global time. When a tempo-sync parameter is engaged on an LFO or delay, its rate is expressed
as a beat division (e.g. "dotted quarter" = 1.5 beats) and recalculated whenever the clock tempo
changes — the same patch sounds different at 80 BPM versus 140 BPM.

---

## 2  Waveforms and Subtractive Synthesis

### 2.1  COMPOSITE_GENERATOR waveform slots

`COMPOSITE_GENERATOR` exposes four waveforms through its internal `WaveformMixer`:

```
slot 0 → sawtooth    (all harmonics, 1/n amplitude — brass, strings)
slot 1 → pulse/square (odd harmonics only — clarinet, hollow, nasal)
slot 2 → sub-octave   (one octave below pitch — fattens bass, body)
slot 3 → sine         (fundamental only — flute, sine bass, ring mod carrier)
```

Set gains via patch JSON:
```json
"VCO": { "saw_gain": 1.0, "pulse_gain": 0.0, "sub_gain": 0.5, "sine_gain": 0.0 }
```

Or from C++:
```cpp
auto gen = std::make_unique<CompositeGenerator>(sample_rate);
gen->mixer().set_gain(0, 1.0f);   // sawtooth full
gen->mixer().set_gain(2, 0.5f);   // sub-octave at half
gen->set_frequency(440.0);
```

### 2.2  Clarinet and hollow woodwind timbre

*(Practical Synthesis Vol. 2, Ch. 1)*

The square/pulse wave (50% duty cycle) produces only **odd harmonics** (1, 3, 5, 7...).
This hollow spectrum is the acoustic basis of the clarinet — it lacks the even harmonics
that give oboe and saxophone their fuller, buzzier tone.

```json
"VCO": { "pulse_gain": 1.0, "pulse_width": 0.5 }
```

With a LP filter at 2000–2400 Hz and `resonance` ≈ 0.3, the result is a warm, woody tone.
Use a fast attack (≤15ms), `sustain=1.0`, and short release for realistic woodwind articulation:

```json
"VCF": { "cutoff": 2200.0, "resonance": 0.3 },
"ENV": { "attack": 0.012, "decay": 0.0, "sustain": 1.0, "release": 0.06 }
```

Compare: `saw_gain=1.0` gives oboe/violin (all harmonics, bright); `pulse_gain=1.0` gives
clarinet/bassoon (odd harmonics, hollow).

See `clarinet.json` for the complete patch.

### 2.3  Noise sources

`WHITE_NOISE` generates flat-spectrum noise in `[-1, 1]`.  It is a `PORT_AUDIO` generator — place
it as the first node in the signal chain and connect its `audio_out` to the filter:

```json
{ "type": "WHITE_NOISE", "tag": "SRC" }
```

Pink noise (−3 dB/octave rolloff) is approximated by routing white noise through a shallow LPF:

```json
"VCF": { "cutoff": 8000.0, "resonance": 0.0 }
```

---

## 3  Pitch and Modulation

### 3.1  Setting pitch — note_on

`Voice::note_on(frequency_hz)` dispatches the frequency to all `COMPOSITE_GENERATOR` and
`DRAWBAR_ORGAN` nodes in the chain.  No V/Oct conversion is needed; the library works in Hz.

### 3.2  VCF keyboard tracking

All VCF modules (`MOOG_FILTER`, `DIODE_FILTER`, `SH_FILTER`, `MS20_FILTER`) declare a `kybd_cv`
port that is **automatically injected by `Voice`** on every `note_on`.  The injection scales
the base cutoff logarithmically with pitch (1V/oct):

```
effective_cutoff = base_cutoff × 2^(kybd_cv)
```

No JSON connection is required.  Keyboard tracking is on by default whenever a VCF is present in
the signal chain.

### 3.3  LFO vibrato

Wire `LFO.control_out → VCO.pitch_cv` for pitch vibrato:

```json
{
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
    "LFO1": { "rate": 5.5, "intensity": 0.003 }
  }
}
```

`intensity` is pitch deviation in semitones.

| Depth   | `intensity` | Character                |
|---------|-------------|--------------------------|
| Subtle  | 0.002–0.005 | Natural vibrato          |
| Medium  | 0.05–0.1    | Expressive, theatrical   |
| Deep    | 0.5–2.0     | FM-like tonal distortion |

### 3.4  LFO filter sweep

Wire `LFO.control_out → VCF.cutoff_cv` for a swept filter (wah, wind, evolving pad):

```json
{ "from_tag": "LFO1", "from_port": "control_out", "to_tag": "VCF", "to_port": "cutoff_cv" }
```

### 3.5  Portamento / glide

Portamento is implemented by `MATHS` — the slew limiter.  Place `MATHS` in the mod-source chain
and wire its `cv_out → VCO.pitch_cv`.  Set `rise` and `fall` times (seconds) to control glide
speed:

```json
{
  "chain": [
    { "type": "MATHS",               "tag": "SLEW" },
    { "type": "COMPOSITE_GENERATOR", "tag": "VCO"  },
    ...
  ],
  "connections": [
    { "from_tag": "SLEW", "from_port": "cv_out", "to_tag": "VCO", "to_port": "pitch_cv" }
  ],
  "parameters": {
    "SLEW": { "rise": 0.15, "fall": 0.10, "curve": 1 }
  }
}
```

`curve`: 0 = linear glide, 1 = exponential (more natural, closer to real portamento).

### 3.6  LFO filter sweep for tone-color modulation

*(Practical Synthesis Vol.1 §2-3 — "growl" and wind-breath effects)*

Routing the LFO to `VCF.cutoff_cv` at low rate produces a slow sweep that continuously changes
tonal color.  At rates below 2 Hz this creates "growl" on brass or woodwind patches.  At very
slow rates (0.1–0.3 Hz) it creates the heaving swell of wind and surf:

```json
"LFO1": { "rate": 0.4, "intensity": 0.4 }
```

Combine with `ENV → VCF.cutoff_cv` (dual routing) to have both a static envelope shape and
ongoing LFO motion simultaneously — the two signals add at the `cutoff_cv` port.

### 3.7  Pitch glide via inverted envelope (whistling technique)

*(Practical Synthesis Vol.1 §3-1, Fig 3-5/3-6)*

A human voice or whistler "finds" its pitch from slightly below.  Model this with an AD envelope
routed through `INVERTER` to `VCO.pitch_cv`:

```
AD_ENVELOPE (attack=1ms, decay=0.4s) → INVERTER (scale=−0.3) → VCO.pitch_cv
```

At note_on the envelope peaks instantly then decays over 0.4s.  The inverter maps this 1→0 ramp
to −0.3→0 pitch offset, so the VCO starts 0.3 octaves below target and slides up over 400ms.
The `AD_ENVELOPE` (not `ADSR_ENVELOPE`) is used because the glide should be a one-shot event
that completes regardless of note duration.

See `whistling.json` for the complete patch.

### 3.8  PWM — pulse-width modulation

`COMPOSITE_GENERATOR` exposes a `pwm_cv` port for modulating the pulse width of its pulse
oscillator in real time.  Wire an LFO to add shimmer or to thicken the sound with beating:

```json
"connections": [
  { "from_tag": "LFO1", "from_port": "control_out", "to_tag": "VCO", "to_port": "pwm_cv" }
],
"parameters": {
  "LFO1": { "rate": 0.7, "intensity": 0.2 },
  "VCO":  { "pulse_gain": 1.0, "pulse_width": 0.5 }
}
```

`pulse_width` sets the static duty cycle (0.0–0.5, where 0.5 = square wave).  The LFO sweeps
it around that value.  PWM produces the classic analogue string/choir shimmer.

---

## 4  Loudness and Envelopes

### 4.1  ADSR_ENVELOPE

The four-stage envelope implements the Roland §4-6 model.  Times are in **seconds**; `sustain` is
a level in [0, 1].

```json
"ENV": { "attack": 0.005, "decay": 0.15, "sustain": 0.6, "release": 0.25 }
```

Common sound families:

| Sound      | Attack | Decay | Sustain | Release | Notes                          |
|------------|--------|-------|---------|---------|--------------------------------|
| Piano      | 0.003  | 0.8   | 0.0     | 0.05    | Use `AD_ENVELOPE` instead      |
| Brass      | 0.08   | 0.0   | 1.0     | 0.15    | Slow attack, full sustain      |
| Strings    | 0.25   | 0.0   | 1.0     | 0.35    | Slow bow attack and release    |
| Ensemble   | 0.35   | 0.1   | 0.85    | 0.5     | Group strings character        |
| Flute      | 0.05   | 0.0   | 1.0     | 0.12    | Moderate attack, full sustain  |
| Organ      | 0.003  | 0.0   | 1.0     | 0.003   | Instant attack and release     |
| Wind/Surf  | 0.8    | 0.0   | 1.0     | 1.5     | Very slow noise swell          |

### 4.2  AD_ENVELOPE for percussive sounds

When sustain=0 (piano, plucked strings, drums), `AD_ENVELOPE` immediately begins decay on note-on
regardless of when note-off fires — ideal for percussive patches:

```json
{ "type": "AD_ENVELOPE", "tag": "ENV" }
```
```json
"ENV": { "attack": 0.001, "decay": 0.4 }
```

### 4.3  Dual envelope routing — the "bright pluck" trick

Routing the same envelope to both `VCA.gain_cv` and `VCF.cutoff_cv` makes the tone brightest at
the note's loudest point and darkens naturally as it decays — exactly how plucked strings behave:

```json
"connections": [
  { "from_tag": "ENV", "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv"   },
  { "from_tag": "ENV", "from_port": "envelope_out", "to_tag": "VCF", "to_port": "cutoff_cv" }
]
```

Used in `pizzicato_violin.json` and `banjo.json`.

### 4.4  Tonal percussion cutoff sweep (bongo technique)

*(Practical Synthesis Vol. 2, Fig 3-22 — bongo drums)*

Routing `AD_ENVELOPE` to **both** `VCA.gain_cv` and `VCF.cutoff_cv` produces a transient
brightness that decays alongside the amplitude — very effective for tonal drums and ethnic
percussion.  The envelope opens the filter wide at the moment of impact and narrows it as the
sound decays, mimicking the physics of a struck drum head:

```json
"connections": [
  { "from_tag": "ENV", "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv"   },
  { "from_tag": "ENV", "from_port": "envelope_out", "to_tag": "VCF", "to_port": "cutoff_cv" }
]
```

Keyboard tracking (`kybd_cv` auto-injected) shifts the VCF resonance peak with pitch, so the
drum's tonal center tracks naturally across the keyboard.

See `bongo_drums.json` (triangle VCO → SH_FILTER, decay=180ms).

### 4.5  Atmospheric / noise envelopes (slow attack, long release)

*(Practical Synthesis Vol. 2, Fig 3-25 — rain / thunder effects)*

Noise-based atmospheric textures benefit from very slow attack and release times to produce
smooth fades.  The ADSR `sustain=1.0` keeps the texture at full level while the gate is held;
long `release` provides the gradual fade-away:

```json
"ENV": { "attack": 0.5, "decay": 0.1, "sustain": 1.0, "release": 2.0 }
```

| Effect | Attack | Release | Character                                   |
|--------|--------|---------|---------------------------------------------|
| Rain   | 0.5s   | 2.0s    | Slow onset, gentle fade; bandpass noise      |
| Wind   | 0.8s   | 1.5s    | Heaving swell; LFO on VCF adds breath motion |
| Surf   | 1.2s   | 3.0s    | Ocean wave — very slow cycles                |

See `rain.json` and `wind_surf.json` for complete patches.

### 4.6  Amplitude modulation / tremolo

Wire `LFO.control_out → VCA.gain_cv` for tremolo.  Set `VCA.initial_gain` so the LFO modulates
around a non-zero midpoint:

```json
"connections": [
  { "from_tag": "LFO1", "from_port": "control_out", "to_tag": "VCA", "to_port": "gain_cv" }
],
"parameters": {
  "LFO1": { "rate": 5.0, "intensity": 0.3 },
  "VCA":  { "initial_gain": 0.7 }
}
```

---

## 5  Filters

### 5.1  Choosing a filter model

| Filter type     | Roland character             | Library module      | Slope         |
|-----------------|------------------------------|---------------------|---------------|
| Standard LPF    | Warm, saturating             | `MOOG_FILTER`       | −24 dB/oct    |
| Roland/TB-303   | Rubbery, acid                | `DIODE_FILTER`      | −24 dB/oct    |
| SH-101/Jupiter  | Liquid, clean                | `SH_FILTER`         | −24 dB/oct    |
| Korg MS-20      | Aggressive, screaming        | `MS20_FILTER`       | −12 dB/oct ×2 |
| High-pass       | Brightens, removes mud       | `HIGH_PASS_FILTER`  | −12 dB/oct    |
| Band-pass       | Formant / wah                | `BAND_PASS_FILTER`  | −12 dB/oct    |

All LPF-family filters share three CV inputs:
- `cutoff_cv` — LFO/envelope control (1V/oct, bipolar)
- `res_cv`    — additive resonance from CV (unipolar)
- `kybd_cv`   — keyboard tracking (auto-injected, no connection needed)

### 5.2  The canonical subtractive patch

```json
{
  "version": 2,
  "name": "Basic Subtractive",
  "groups": [{
    "id": 0,
    "chain": [
      { "type": "COMPOSITE_GENERATOR", "tag": "VCO" },
      { "type": "MOOG_FILTER",         "tag": "VCF" },
      { "type": "ADSR_ENVELOPE",       "tag": "ENV" },
      { "type": "VCA",                 "tag": "VCA" }
    ],
    "connections": [
      { "from_tag": "VCO", "from_port": "audio_out",    "to_tag": "VCF", "to_port": "audio_in" },
      { "from_tag": "VCF", "from_port": "audio_out",    "to_tag": "VCA", "to_port": "audio_in" },
      { "from_tag": "ENV", "from_port": "envelope_out", "to_tag": "VCA", "to_port": "gain_cv"  }
    ],
    "parameters": {
      "VCO": { "saw_gain": 1.0 },
      "VCF": { "cutoff": 1200.0, "resonance": 0.2 },
      "ENV": { "attack": 0.01, "decay": 0.2, "sustain": 0.7, "release": 0.3 }
    }
  }]
}
```

### 5.3  Two-filter series chains for double-reed formants

*(Practical Synthesis Vol.1 §1-3, Fig 1-9 — Oboe / English Horn)*

Placing a low-pass filter in series with a high-pass filter creates a narrow bandpass response
that mimics the vocal-tract formants of reed instruments.  The LP sets the upper limit of the
nasal band; the HP strips the thick low-end that makes the instrument sound more cello-like:

```
VCO (saw) → SH_FILTER (LP, cutoff≈1800 Hz) → HIGH_PASS_FILTER (HP, cutoff≈200 Hz) → VCA
```

```json
"chain": [
  { "type": "COMPOSITE_GENERATOR", "tag": "VCO" },
  { "type": "SH_FILTER",           "tag": "VCF" },
  { "type": "HIGH_PASS_FILTER",    "tag": "HPF" },
  { "type": "ADSR_ENVELOPE",       "tag": "ENV" },
  { "type": "VCA",                 "tag": "VCA" }
]
```

| Reed character | LP cutoff | HP cutoff | LP resonance |
|----------------|-----------|-----------|--------------|
| English Horn   | 1800 Hz   | 200 Hz    | 0.5          |
| Oboe           | 2400 Hz   | 300 Hz    | 0.6          |
| Bassoon        | 900 Hz    | 120 Hz    | 0.35         |

See `english_horn.json` for the complete patch.

### 5.4  Resonant noise as pitched percussion

*(Practical Synthesis Vol.1 §3-3)*

A highly resonant filter driven by white noise produces a pitched, drum-like sound.  The filter's
self-resonance peak dominates the spectrum, giving the noise a tonal centre that tracks keyboard
pitch via the auto-injected `kybd_cv`:

```
WHITE_NOISE → SH_FILTER (res≈0.7–0.9, cutoff sets pitch range) → AD → VCA
```

Higher `resonance` → more pitched, more sustained ring.  Lower → more "whoosh", less tone.
`AD_ENVELOPE` with a short decay (0.05–0.15s) gives the characteristic dry knock of wood or drum.

See `wood_blocks.json` (cutoff=800 Hz, res=0.7, decay=0.12s) and `percussion_noise.json`.

### 5.5  Two-filter cascade for noise percussion (snare / bass drum)

*(Practical Synthesis Vol. 2, §3-3 snare and §3 bass drum)*

Filtered white noise is the basis of most electronic drum sounds.  The filter type and cutoff
determine the character:

**Snare drum** — LP + HPF creates a mid-frequency bandpass noise burst:
```
WHITE_NOISE → SH_FILTER (LP, cutoff=3500 Hz, res=0.4)
           → HIGH_PASS_FILTER (cutoff=180 Hz) → AD (decay=150ms) → VCA
```

**Bass drum** — single deep LP creates a warm thud:
```
WHITE_NOISE → MOOG_FILTER (cutoff=280 Hz, res=0.55) → AD (decay=300ms) → VCA
```

| Drum type  | Filter(s)                          | Decay   | Character              |
|------------|------------------------------------|---------|------------------------|
| Bass drum  | MOOG LP 280 Hz, res=0.55           | 0.3s    | Deep warm thud         |
| Snare drum | LP 3500 Hz + HPF 180 Hz            | 0.15s   | Mid-frequency snap     |
| Wood block | SH LP 800 Hz, res=0.7              | 0.12s   | Dry resonant knock     |
| Cymbal     | MOOG HP 2 + LP 6000 Hz             | 0.35s   | Bright metallic hiss   |

The `AD_ENVELOPE` is used for all noise percussion because the decay must complete regardless
of gate duration — essential for realistic percussive behaviour.

See `snare_drum.json`, `bass_drum.json`, `wood_blocks.json`, and `cymbal.json`.

### 5.6  Two-VCO cowbell (TR-808 technique)

*(Roland TR-808 circuit analysis — two detuned square waves)*

The TR-808 cow bell uses two square-wave oscillators tuned a perfect 5th apart (7 semitones),
mixed together and high-pass filtered to remove the low thump.  The interval creates the
inharmonic beating pattern that gives the instrument its metallic "clank":

```
VCO1 (pulse) ─┐
               ├─▶ AUDIO_MIXER ─▶ HIGH_PASS_FILTER (200 Hz) ─▶ AD ─▶ VCA
VCO2 (pulse,  ─┘
  +7 semitones)
```

```json
"VCO1": { "pulse_gain": 1.0 },
"VCO2": { "pulse_gain": 1.0, "transpose": 7 },
"MIX":  { "gain_1": 1.0, "gain_2": 0.8 },
"HPF":  { "cutoff": 200.0, "resonance": 0.1 },
"ENV":  { "attack": 0.001, "decay": 0.22 }
```

The perfect 5th (7 semitones) produces sum and difference frequencies between the pulse
wave harmonics, creating the characteristic metallic overtone cluster.  The HPF strips the
fundamental bass content so only the upper metallic partials remain.

See `cow_bell.json` for the complete patch.

### 5.7  Filter resonance and self-oscillation

`resonance` ranges 0–1.  At ≥ ~0.95 (filter-dependent), `MOOG_FILTER` and `MS20_FILTER`
self-oscillate — producing a pitched sine at the cutoff frequency.  This is the "special effect"
described in Roland §5-10.

```json
"VCF": { "cutoff": 800.0, "resonance": 0.97 }
```

---

## 6  Effects

### 6.1  Echo / delay

`ECHO_DELAY` provides BBD-style delay with optional LFO modulation (shimmer):

```json
{ "type": "ECHO_DELAY", "tag": "DLY" },
"parameters": {
  "DLY": { "delay_ms": 400.0, "feedback": 0.45, "mix": 0.5 }
}
```

### 6.2  Reverb

Two reverb algorithms are available:

| Module           | Character                        | Use case               |
|------------------|----------------------------------|------------------------|
| `REVERB_FREEVERB`| Dense, lush (Schroeder/Moorer)   | Pads, strings, vocals  |
| `REVERB_FDN`     | Precise T60, natural decay       | Halls, rooms           |

```json
"parameters": {
  "REV": { "room_size": 0.75, "damping": 0.4, "mix": 0.35 }
}
```

### 6.3  Phaser

`PHASER` is a 4/8-stage all-pass ladder with quadrature stereo LFO:

```json
"parameters": {
  "PH": { "rate": 0.5, "depth": 0.6, "feedback": 0.4, "mix": 0.5 }
}
```

### 6.4  Chorus

`JUNO_CHORUS` emulates the Roland Juno-60 BBD chorus with dual-rate stereo modulated delay.
Add it to the signal chain after the filter:

```json
{ "type": "JUNO_CHORUS", "tag": "CHR" }
```

Connect the preceding signal into the chorus input:
```json
{ "from_tag": "VCF", "from_port": "audio_out", "to_tag": "CHR", "to_port": "audio_in" }
```

```json
"parameters": {
  "CHR": { "mode": 1, "rate": 0.5, "depth": 0.5 }
}
```

`mode`: 0=Off, 1=Mode I (~0.4 Hz, subtle), 2=Mode II (~0.6 Hz, moderate), 3=I+II (~1.0 Hz, wide).
`depth` [0, 1] controls modulation depth (scales 0–3ms delay swing).

The chorus is inherently stereo — the left and right channels receive opposing LFO phases,
producing the characteristic Juno-60 stereo spread.  Used in `juno_pad.json`.

### 6.5  Distortion / Overdrive

`DISTORTION` is a guitar/pedal-style saturator with 4× oversampling anti-aliasing. Place it
after the VCA to replicate an instrument output plugged into a distortion pedal:

```json
{ "type": "DISTORTION", "tag": "DIST" }
```

```json
"DIST": { "drive": 8.0, "character": 0.3 }
```

| Parameter   | Range | Effect                                                                 |
|-------------|-------|------------------------------------------------------------------------|
| `drive`     | 1–40  | Input gain into the clipping stage. 1 = near-clean; 20+ = near-square |
| `character` | 0–1   | 0 = symmetric tanh (warm, odd harmonics); 1 = asymmetric clip (+0.5/−1.0, even harmonics, 6 dB asymmetry) |

**TB-303 pedal topology** (acid_reverb.json):

```
VCO → DIODE_FILTER → VCA → DISTORTION → REVERB_FDN
```

The envelope drives both `VCA.gain_cv` and `DIODE_FILTER.cutoff_cv`. `DISTORTION` is after
the VCA so the amp envelope shapes the level before saturation — exactly as if you plugged the
303 output into a pedal. Increase `drive` for heavier grit; increase `character` past 0.5 to
introduce even harmonics and asymmetric clipping.

### 6.6  TB-303 / Acid synthesis

The acid bass sound relies on the `DIODE_FILTER` maintaining filter state **across notes** —
resonance builds up as the riff fires rapid-fire gates. The key parameters:

| Parameter          | Value   | Why                                                              |
|--------------------|---------|------------------------------------------------------------------|
| `VCF.resonance`    | 0.90–0.97 | On the edge of self-oscillation — the 303 squelch             |
| `VCF.env_depth`    | 2–4     | How many octaves the AD envelope sweeps the cutoff              |
| `VCF.cutoff`       | 400–1200 Hz | Base cutoff before envelope sweep                            |
| `ENV.attack`       | 0.002–0.005 | Very fast — the filter snaps open on each note              |
| `ENV.decay`        | 0.1–0.4 | Controls the squelch tail length                               |
| `DIST.drive`       | 4–15    | Moderate drive for grit without losing note definition          |
| `DIST.character`   | 0–0.5   | Low values keep it warm; higher values for transistor edge      |

See `patches/acid_reverb.json` for the canonical implementation.

---

## 7  CV Utilities

### 7.1  CV_MIXER — attenuverter

Mixes up to 4 bipolar CV sources with individual gain (−1.0 to +1.0) and a DC offset:

```json
{ "type": "CV_MIXER", "tag": "MIX" }
```

### 7.2  CV_SPLITTER — fan-out

Routes one CV source to up to 4 destinations with per-output gain scaling.

### 7.3  SAMPLE_HOLD

Freezes `cv_in` on each rising clock edge.  Wire an LFO square wave as the clock for classic S&H
random modulation:

```json
{ "from_tag": "CLK", "from_port": "control_out", "to_tag": "SH", "to_port": "clock_in" },
{ "from_tag": "SRC", "from_port": "control_out", "to_tag": "SH", "to_port": "cv_in"    },
{ "from_tag": "SH",  "from_port": "cv_out",      "to_tag": "VCF","to_port": "cutoff_cv"}
```

### 7.4  GATE_DELAY

Delays the note-on gate by a fixed time before firing.  Use for delayed-vibrato effects (LFO only
kicks in after the initial note attack has settled):

```json
{ "type": "GATE_DELAY", "tag": "GD" },
"parameters": { "GD": { "delay_ms": 400.0 } }
```

### 7.5  INVERTER

Inverts a CV signal (multiplies by −1.0 default scale).  Useful for inverted filter envelopes
(harpsichord — filter closes as the envelope opens):

```json
{ "from_tag": "ENV", "from_port": "envelope_out", "to_tag": "INV", "to_port": "cv_in"    },
{ "from_tag": "INV", "from_port": "cv_out",       "to_tag": "VCF", "to_port": "cutoff_cv"}
```

---

## 8  Ring Modulation (Bell, Metallic Textures)

`RING_MOD` multiplies two audio signals.  The output contains only the sum and difference
sidebands — neither carrier appears.  Two sinusoids at a non-octave interval produce inharmonic
bell/gong partials.

```
VCO1 (sine, root pitch)  ─┐
                            ├─▶ RING_MOD ─▶ VCA ← ENV
VCO2 (sine, transposed)  ─┘
```

```json
{ "from_tag": "VCO1", "from_port": "audio_out", "to_tag": "RM", "to_port": "audio_in_a" },
{ "from_tag": "VCO2", "from_port": "audio_out", "to_tag": "RM", "to_port": "audio_in_b" }
```

| VCO2 `transpose` | `detune` | Character                             |
|------------------|----------|---------------------------------------|
| 3                | 20       | Glockenspiel (minor 3rd shimmer)      |
| 7                | 40       | Gong (perfect 5th, long decay=3.5s)   |
| 12               | 0        | Octave (sidebands suppress)           |
| 19               | 63       | Bell / tubular (minor tenth + detune) |
| 24               | 40       | Large gong / tam-tam                  |

The `detune` parameter (cents) introduces closely-spaced beat frequencies between the
sidebands.  For bar and bell instruments this shimmer is musically desirable — it matches
the natural slight inharmonicity of a real struck metal bar.

**Gong character** comes from the long `AD_ENVELOPE` decay (3.5s+) rather than the exact
interval.  Use transpose=7 (perfect 5th) for a more harmonically related shimmer; use
transpose=19+ for a more dissonant, tam-tam quality.

See `bell.json` (transpose=19), `glockenspiel.json` (transpose=3), and `gong.json`
(transpose=7, decay=3.5s) for complete patches.

---

## 9  Patch Catalog

All 37 reference patches in the `patches/` directory:

### Melodic / Tonal

| Patch                | File                     | Topology / Character                                            | Source            |
|----------------------|--------------------------|-----------------------------------------------------------------|-------------------|
| SH-101 Bass          | `sh_bass.json`           | VCO(saw+sub) → SH_FILTER → ADSR → VCA                          | Foundation §3-2   |
| TB-303 Acid Bass     | `tb_bass.json`           | VCO(pulse) → DIODE_FILTER → ADSR → VCA + resonant sweep        | Foundation §3-3   |
| Acid Reverb          | `acid_reverb.json`       | VCO(saw) → DIODE_FILTER → VCA → DISTORTION → REVERB_FDN, AD envelope sweeps VCA+VCF | see §6.6 |
| Bowed Bass           | `bowed_bass.json`        | VCO(saw+tri) → ADSR (slow bow) → VCA                           | Foundation §4-3   |
| Brass / Horn         | `brass.json`             | VCO(saw+pulse) → ADSR → VCA, medium attack                     | Foundation §4-2   |
| Flute                | `flute.json`             | VCO(sine) → ADSR → VCA + LFO vibrato                           | Foundation §4-5   |
| Violin (vibrato)     | `violin_vibrato.json`    | VCO(saw) → ADSR → VCA + LFO pitch vibrato                      | Foundation §4-4   |
| Pizzicato Violin     | `pizzicato_violin.json`  | VCO(saw+sine) → SH_FILTER → AD (dual ENV→VCF+VCA)              | Foundation §4-4   |
| Group Strings        | `group_strings.json`     | VCO(saw+sub) → MOOG_FILTER → ADSR (slow attack) + LFO          | Foundation §4-6   |
| Harpsichord          | `harpsichord.json`       | VCO → VCF (inverted ENV sweep) → AD → VCA                      | Foundation §4-1   |
| Juno Pad             | `juno_pad.json`          | VCO(saw+sine) → ADSR → VCA + slow LFO vibrato                  | Foundation §5-8   |
| Reverb Pad           | `reverb_pad.json`        | VCO → ADSR → VCA + REVERB tail                                  | Foundation §6-5   |
| Drawbar Organ        | `organ_drawbar.json`     | DRAWBAR_ORGAN → ADSR → VCA (9 Hammond harmonics)               | Foundation §3-6   |
| Banjo                | `banjo.json`             | VCO1(saw)+VCO2(pulse) → AUDIO_MIXER → HPF → SH_FILTER → AD (dual ENV→VCF+VCA) | Foundation §2-6 |
| Dog Whistle          | `dog_whistle.json`       | VCO(sine) + ENV_PITCH(up-glide) + ENV_AMP → VCA                | Foundation §5-9   |
| Clarinet             | `clarinet.json`          | VCO(pulse, 50%) → SH_FILTER (LP) → ADSR → VCA                  | Practical 2 Ch. 1 |
| English Horn         | `english_horn.json`      | VCO(saw) → SH_FILTER → HIGH_PASS_FILTER → ADSR → VCA           | Practical 1 §1-3  |
| Whistling            | `whistling.json`         | VCO(sine) + PITCH_ENV→INV→pitch_cv + SH_FILTER → ADSR → VCA    | Practical 1 §3-1  |

### Melodic Percussion / Bell

| Patch                | File                     | Topology / Character                                            | Source            |
|----------------------|--------------------------|-----------------------------------------------------------------|-------------------|
| Bell                 | `bell.json`              | VCO1×VCO2 (RING_MOD, transpose=19) → AD → VCA                  | Foundation §5-6   |
| Glockenspiel         | `glockenspiel.json`      | VCO1×VCO2 (RING_MOD, transpose=3, detune=8) → AD (0.35s) → VCA | Practical 1 §1-4  |
| Gong                 | `gong.json`              | VCO1×VCO2 (RING_MOD, transpose=7, detune=40) → AD (3.5s) → VCA | Practical 2 Fig 3-17 |

### Noise / Atmospheric

| Patch                | File                     | Topology / Character                                            | Source            |
|----------------------|--------------------------|-----------------------------------------------------------------|-------------------|
| Wind / Surf          | `wind_surf.json`         | WHITE_NOISE → MOOG_FILTER (LFO sweep) → ADSR → VCA             | Foundation §5-5   |
| Percussion Noise     | `percussion_noise.json`  | WHITE_NOISE → AD → VCA                                         | Foundation §5-7   |
| Rain                 | `rain.json`              | WHITE_NOISE → MOOG LP → HIGH_PASS_FILTER → ADSR → VCA          | Practical 2 Fig 3-25 |
| Thunder              | `thunder.json`           | Two WHITE_NOISE paths → AUDIO_MIXER → SH_FILTER → AD → VCA     | Practical 2 Fig 3-24 |

### Percussion

| Patch                | File                     | Topology / Character                                            | Source            |
|----------------------|--------------------------|-----------------------------------------------------------------|-------------------|
| Cymbal               | `cymbal.json`            | WHITE_NOISE → MOOG HPF+LP → ADSR (decay=1.2s) → ECHO_DELAY → VCA | Foundation §5-7 |
| Wood Blocks          | `wood_blocks.json`       | WHITE_NOISE → SH_FILTER (res=0.7) → AD (0.12s) → VCA           | Practical 1 §3-3  |
| Snare Drum           | `snare_drum.json`        | WHITE_NOISE → SH_FILTER LP → HIGH_PASS_FILTER → AD → VCA       | Practical 2 §3-3  |
| Bass Drum            | `bass_drum.json`         | WHITE_NOISE → MOOG_FILTER (LP 280 Hz, res=0.55) → AD → VCA     | Practical 2 §3    |
| Bongo Drums          | `bongo_drums.json`       | VCO(tri) → SH_FILTER → AD (dual ENV→VCF+VCA, kybd tracked)     | Practical 2 Fig 3-22 |
| Cow Bell             | `cow_bell.json`          | VCO1(pulse)+VCO2(pulse, +7st) → AUDIO_MIXER → HPF(200Hz) → AD → VCA | TR-808 style    |
| Tom Tom              | `tom_tom.json`           | VCO1 → VCO2.fm_in (audio-rate FM) → SH_FILTER → AD → VCA      | Practical 2 Fig 3-14 |

### Ensemble / Multi-Module (Phase 26)

| Patch                | File                        | Topology / Character                                         | Source            |
|----------------------|-----------------------------|--------------------------------------------------------------|-------------------|
| Group Strings        | `group_strings.json`        | Dual VCO (detuned) → AUDIO_MIXER → MOOG_FILTER → ADSR → VCA + LFO | Foundation §4-6 |
| Juno Strings         | `juno_strings.json`         | VCO(saw+sub) → SH_FILTER → ADSR → VCA → JUNO_CHORUS        | Juno-60 style     |
| Strings Chorus Reverb| `strings_chorus_reverb.json`| VCO(saw) → MOOG_FILTER → ADSR → VCA → JUNO_CHORUS → REVERB_FREEVERB | Lush stereo strings |
| Delay Lead           | `delay_lead.json`           | VCO(saw+pulse) → SH_FILTER → ADSR → VCA → ECHO_DELAY       | —                 |
| Gong (full)          | `gong_full.json`            | RING_MOD + WHITE_NOISE → AUDIO_MIXER → AD → VCA             | Practical 2 Fig 3-17 |
| Gong Noise Layer     | `gong_noise_layer.json`     | RING_MOD + noise → AUDIO_MIXER → MOOG_FILTER → AD → VCA    | Practical 2 Fig 3-17 |

---

## 10  Building a Patch — Step by Step

1. **Choose a source** — `COMPOSITE_GENERATOR` for pitched sounds, `WHITE_NOISE` for noise.
2. **Add a filter** — select based on character (see §5.1).
3. **Add an envelope** — `ADSR_ENVELOPE` for sustained sounds, `AD_ENVELOPE` for percussive.
4. **Add a VCA** — always the last node in the audio chain.
5. **Wire the envelope** — `ENV.envelope_out → VCA.gain_cv` (always required).
6. **Add modulation** — `LFO` → `pitch_cv` (vibrato), `LFO` → `cutoff_cv` (filter sweep).
7. **Use dual envelope routing** for plucked/percussive timbre shaping (`ENV → VCF.cutoff_cv`).
8. **Call `bake()`** to validate (or rely on `engine_load_patch()` which calls it automatically).

---

## 11  Automated Test Verification

The library ships four analysis components in `src/dsp/analysis/` that support in-process
audio verification — both in the functional test suite and in production health-check code.

| Header                | Class / function         | Purpose                                          |
|-----------------------|--------------------------|--------------------------------------------------|
| `AudioTap.hpp`        | `audio::AudioTap`        | RT-safe tee: captures audio inline from the chain|
| `DctProcessor.hpp`    | `audio::DctProcessor`    | DCT-II transform with Hann window                |
| `PitchDetector.hpp`   | `audio::PitchDetector`   | Dominant-frequency extraction via peak + parabolic interp |
| `SpectralAnalysis.hpp`| `audio::spectral_centroid`| Frequency-weighted mean of the DCT spectrum     |

### 11.1  Choosing the right assertion tool

| Sound type / goal                            | Correct tool              | Reason                                                       |
|----------------------------------------------|---------------------------|--------------------------------------------------------------|
| Oscillator: is the pitch correct?            | `PitchDetector::detect()` | Peak-picks the dominant DCT bin; parabolic interp ≈ ±1 Hz   |
| Filter active: energy below/above cutoff?    | `spectral_centroid()`     | Centroid < LP cutoff; centroid > HP cutoff                   |
| Timbral change direction (glide, sweep)      | `spectral_centroid()`     | Compare two windows: centroid should shift in expected direction |
| Ring-mod / inharmonic content                | `spectral_centroid()`     | Centroid deviates from fundamental — no single clear peak    |
| Keyboard tracking                            | `spectral_centroid()`     | Capture C3 vs C5 separately; higher note → higher centroid   |
| Noise percussion (snare, bass drum)          | RMS onset/tail ratio      | No deterministic pitch; decay shape is the relevant property |
| Attack / sustain / decay progression         | RMS per block             | Signal amplitude over time; loop and accumulate              |
| "Is there audio at all?"                     | RMS smoke test            | Simplest check; threshold > 0.001                            |

**`PitchDetector` is reliable when:**
- The signal has a clean dominant fundamental (pure VCO, simple VCO+LP filter)
- The window contains enough cycles: `N / f ≥ 3` (e.g., 440 Hz needs 100-sample window minimum — 2048 is generous)
- The signal is not noisy enough to elevate spurious bins above the fundamental

**`PitchDetector` is unreliable when:**
- The source is white noise or heavily noise-dominated
- Ring-mod output (sidebands, not a fundamental)
- The fundamental is strongly suppressed by a HP filter (sub-bass with HPF)
- Very heavy resonance causes the filter self-oscillation peak to dominate

**`spectral_centroid` is reliable when:**
- You need a directional or comparative result ("was the centroid higher or lower?")
- The signal is noise — centroid of filtered noise correctly reflects the filter shape
- You are checking the band of energy, not a single pitch

**`spectral_centroid` is unreliable as an absolute pitch:** it returns the *mean* of all
spectral energy, not the dominant peak.  A 440 Hz + 1320 Hz two-tone signal gives a
centroid of ~880 Hz — not the fundamental.

### 11.2  Window size and frequency resolution

`DctProcessor` implements DCT-II with a Hann window. Frequency resolution:

```
f_resolution = sample_rate / (2 × N)
```

| N     | Resolution @ 48kHz | Resolution @ 44.1kHz | Wall time (O(N²)) |
|-------|-------------------|----------------------|-------------------|
| 512   | 46.9 Hz/bin       | 43.1 Hz/bin          | Fast              |
| 2048  | 11.7 Hz/bin       | 10.8 Hz/bin          | Acceptable        |
| 4096  | 5.9 Hz/bin        | 5.4 Hz/bin           | Slow (~4× 2048)   |

Use **N=2048** for most tests — sufficient to distinguish adjacent semitones above ~100 Hz
and acceptable in offline test execution.  Use N=4096 only for bass/sub-bass pitch tests
where the fundamental is ≤ 60 Hz and 1-semitone discrimination is needed.

`PitchDetector` applies parabolic interpolation between bins, achieving sub-bin accuracy
(≈ ±1 Hz at A4 with N=2048).

### 11.3  Using AudioTap in the signal chain

`AudioTap` is a `Processor` that acts as an inline tee — it pulls audio from its input
and simultaneously writes the samples into a lock-free ring buffer.  Insert it between
any two nodes in the chain to tap that signal point.

```cpp
// Insert a tap after the VCO, before the VCF
auto tap = std::make_unique<audio::AudioTap>(8192);  // 8192-sample ring buffer
// Wire: VCO → TAP → VCF
// (AudioTap holds an input_ pointer via Processor::inputs_ like any other node)
```

Reading from the tap after rendering blocks:
```cpp
// After engine_process() calls:
std::vector<float> window(2048);
tap->read(std::span<float>(window.data(), window.size()));

// Now analyze:
audio::DctProcessor dct(2048, 2048);
std::vector<float> mags(2048);
dct.process(window, mags);

float pitch   = audio::PitchDetector::detect(mags, float(sample_rate));
float centroid = audio::spectral_centroid(window, sample_rate);
```

**In functional tests**, the more common pattern avoids `AudioTap` entirely: extract the
left channel directly from the interleaved stereo output buffer, then pass that to
`spectral_centroid` or `DctProcessor + PitchDetector`.  This is simpler and sufficient
when measuring the final output rather than a mid-chain signal:

```cpp
// From the test pattern used throughout the functional test suite:
std::vector<float> buf(BLOCK * 2);  // interleaved stereo
engine_process(engine(), buf.data(), BLOCK);

// Extract mono from interleaved left channel:
std::vector<float> mono;
for (size_t i = 0; i < BLOCK; ++i)
    mono.push_back(buf[i * 2]);

float centroid = spectral_centroid(mono, sample_rate);
```

Use `AudioTap` when you need to measure a **mid-chain** point (e.g., the oscillator output
before the filter, to verify VCO pitch independently of filter resonance effects).

### 11.4  Patterns used in the functional test suite

The full suite uses three distinct patterns, chosen based on signal type:

**Pattern A — RMS smoke + decay (percussive noise):**
Used by `bass_drum`, `snare_drum`, `cymbal`, `wood_blocks`.  Loop N blocks accumulating
`sum_sq`, then compare onset window to tail window.
```
onset_rms / tail_rms ≥ threshold
```

**Pattern B — spectral centroid comparison (timbral / filtered signals):**
Used by `whistling` (PitchGlide), `bongo_drums` (keyboard tracking), `cow_bell`
(metallic range), `english_horn` (formant band).  Capture two windows, compute centroid
of each, assert direction or range.
```
centroid_late > centroid_early × factor   (glide / attack)
centroid > freq_lo && centroid < freq_hi  (bandpass shape)
centroid_high_note > centroid_low_note    (keyboard tracking)
```

**Pattern C — DCT + PitchDetector (absolute pitch, modulation confirmation):**
Used by `dog_whistle` (PitchEnvelopeFiresAndReturns) and `oscillator_baseline_test`.
Capture early and late windows, transform with `DctProcessor`, detect pitch with
`PitchDetector`, assert ordering.
```
pitch_early > pitch_late   (downward glide after peak)
pitch > expected_hz - tol  (oscillator tuning)
```

Choose the simplest pattern that gives a decisive assertion.  Prefer Pattern A for any
noise source.  Prefer Pattern B for timbre or direction checks.  Reserve Pattern C for
cases where an exact or approximate frequency is required.

---

## 12  USB MIDI HAL (Phase 25)

Phase 25 adds a cross-platform MIDI hardware layer following the **identical factory pattern** as the audio HAL — platform-specific code is isolated in concrete driver classes; the bridge and all library code see only the abstract base.

### 12.1  Design Principles

- **No platform `#ifdef`s outside HAL `.cpp` files**: `MidiDriver::create()` and `MidiDriver::enumerate_devices()` are static factory methods declared in the base header and defined only in the platform implementations. The bridge calls these factory methods with no OS-specific includes.
- **Independent from the audio HAL**: `MidiDriver` has no dependency on `AudioDriver`. They share the `hal::` namespace and the same structural pattern but neither owns the other.
- **Input is callback-driven**: MIDI data arrives asynchronously on a driver thread (ALSA: polling thread; CoreMIDI: CoreMIDI callback thread). The callback dispatches raw bytes to the engine via the existing `engine_process_midi_bytes` call — no new engine-side MIDI path is needed.
- **Output is synchronous**: `send_bytes()` calls the OS write API immediately. Do not call from the audio thread.

### 12.2  Platform Coverage

| Platform | Driver class       | OS APIs used                                                       |
|----------|--------------------|-------------------------------------------------------------------|
| Linux    | `AlsaMidiDriver`   | `snd_rawmidi_open/read/write`, polling thread                     |
| macOS    | `CoreMidiDriver`   | `MIDIClientCreate`, `MIDIInputPortCreate` (callback), `MIDISend`  |

All ALSA headers are confined to `AlsaMidiDriver.cpp`; all CoreMIDI headers are confined to `CoreMidiDriver.cpp`. No platform header leaks through `.hpp` files.

### 12.3  C API (`midi_*` family)

```c
// Device enumeration — no handle required
int  host_midi_device_count();
int  host_midi_get_device_info(int index, ...);   // fills HostMidiDeviceInfo fields

// Lifecycle
MidiHandle* midi_open_input(int device_index);
MidiHandle* midi_open_output(int device_index);
void        midi_close(MidiHandle* handle);

// Connect a MIDI input handle to an engine
// Automatically dispatches received bytes via engine_process_midi_bytes
void        midi_connect_to_engine(MidiHandle* midi, EngineHandle* engine);

// Output helpers
int  midi_send_bytes(MidiHandle* handle, const uint8_t* data, int size);
int  midi_send_note_on(MidiHandle* handle, int channel, int note, int velocity);
int  midi_send_note_off(MidiHandle* handle, int channel, int note);
int  midi_send_cc(MidiHandle* handle, int channel, int cc, int value);
int  midi_send_program_change(MidiHandle* handle, int channel, int program);
```

`MidiHandle` is an opaque pointer (same pattern as `EngineHandle`). The connection established by `midi_connect_to_engine` is live until `midi_close` is called.

### 12.4  Typical Usage

```c
// Enumerate devices
int count = host_midi_device_count();
for (int i = 0; i < count; ++i) {
    // inspect HostMidiDeviceInfo.name, .is_input, .is_output
}

// Open a USB MIDI keyboard (input device index 0) and connect to the engine
MidiHandle* kbd = midi_open_input(0);
midi_connect_to_engine(kbd, engine);

// Audio loop drives everything — MIDI bytes arrive asynchronously
// and are injected into the engine by the driver callback.

// On shutdown
midi_close(kbd);
```

---

## 13  hpp/cpp Companion Split, Presets & Module Tooling (Phase 26)

Phase 26 reorganised all 30 processor implementations into co-located `.cpp` files and added build preset support and a patch validation tool.

### 13.1  Co-located `.cpp` Files and Self-Registration

Every processor type now has a companion `.cpp` file (e.g. `src/dsp/fx/DistortionProcessor.cpp`) that holds the constructor body, `do_pull`, and helper methods. Each `.cpp` also has a `kRegistered` static initializer that calls the 4-arg `register_module` overload with a `usage_notes` string — the metadata prerequisite for the Phase 27A introspection API:

```cpp
// src/dsp/fx/DistortionProcessor.cpp (tail)
namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "DISTORTION",
    "Guitar-style distortion with 4x oversampling",
    "Place post-VCA to replicate synth output into a pedal.",
    [](int sr) { return std::make_unique<DistortionProcessor>(sr); }
);
} // namespace
```

`ProcessorRegistrations.cpp` is **retained** as the authoritative registrar called from `engine_create()`. It also serves as "linker bait" — its explicit factory lambdas reference every processor type, ensuring all `.o` files are included in the static library link. All 30 modules are compiled in every build configuration.

### 13.2  CMake Presets

Four named presets are defined in `CMakePresets.json` at the project root. All compile all 30 modules:

| Preset           | Build type  | Tests | Use case                     |
|------------------|-------------|-------|------------------------------|
| `desktop_full`   | Debug       | ON    | Primary development          |
| `desktop_release`| Release     | OFF   | Optimised desktop            |
| `pi_synth`       | Release     | OFF   | Raspberry Pi arm64           |
| `pi_minimal`     | MinSizeRel  | OFF   | Raspberry Pi embedded        |

```bash
# Configure and build
cmake --preset desktop_full
cmake --build --preset desktop_full

# Run tests
ctest --preset desktop_full

# Pi build (supply a toolchain file)
cmake --preset pi_synth -DCMAKE_TOOLCHAIN_FILE=/path/to/arm64.cmake
cmake --build --preset pi_synth
```

### 13.3  Python Configuration Tool

`tools/configure_modules.py` — pure Python 3, no external dependencies — documents and validates module sets for different build targets:

```bash
# List all 30 modules with category and description
python tools/configure_modules.py list

# Show modules in a preset and validate all patches against it
python tools/configure_modules.py preset pi_minimal --validate-patches

# Validate all patches in patches/ against the full module set
python tools/configure_modules.py validate

# Interactive: select modules per category, then validate patches
python tools/configure_modules.py interactive
```

The tool currently models selective module sets for documentation and patch compatibility checking. Actual binary stripping via CMake options is a future phase item.

### 13.4  Handling Missing Modules at Runtime

If a patch JSON references a module type absent from the registry (e.g. because it was excluded from a future stripped build), `ModuleRegistry::create()` returns `nullptr` and logs:

```
[WARN] Module type "REVERB_FDN" not registered — was it excluded at compile time?
       Patch load failed for group 0.
```

The patch is not loaded; the engine is never left in a partially-configured state.

---

## 14  Audio File I/O (Phase 27C)

Phase 27C adds two file-I/O processors and a live-input source, giving host applications the building blocks for offline rendering, sample playback, and future hardware capture.

### 14.1  AUDIO_FILE_READER — WAV/AIFF Playback

Place `AUDIO_FILE_READER` as the first node in a chain to use a file as the audio source. After `bake()`, set the file path via the string parameter API:

```c
engine_add_module(engine, "AUDIO_FILE_READER", "FILE_IN");
engine_add_module(engine, "MOOG_FILTER", "VCF");
engine_add_module(engine, "VCA", "VCA");
engine_connect_ports(engine, "FILE_IN", "audio_out", "VCF", "audio_in");
engine_connect_ports(engine, "VCF",     "audio_out", "VCA", "audio_in");
engine_bake(engine);

engine_set_tag_string_param(engine, "FILE_IN", "path", "/samples/loop.wav");
engine_set_tag_parameter(engine, "FILE_IN", "loop", 1.0f);
engine_start(engine);
```

**Supported formats**: WAV (PCM 16/24/32, float 32/64) and AIFF (PCM 16/24/32, float 32) via libsndfile 1.2.2. Mono and stereo files only. No compressed formats (MP3, AAC, OGG). If the file's sample rate differs from the engine's rate, libsamplerate 0.2.2 performs high-quality SRC off the audio thread before playback begins.

### 14.2  AUDIO_FILE_WRITER — Real-Time WAV Capture

Place `AUDIO_FILE_WRITER` as the last node in a chain to record the processed signal to disk:

```c
engine_add_module(engine, "COMPOSITE_GENERATOR", "VCO");
engine_add_module(engine, "VCA", "VCA");
engine_add_module(engine, "AUDIO_FILE_WRITER", "FILE_OUT");
engine_connect_ports(engine, "VCO",      "audio_out", "VCA",      "audio_in");
engine_connect_ports(engine, "VCA",      "audio_out", "FILE_OUT", "audio_in");
engine_bake(engine);

engine_set_tag_string_param(engine, "FILE_OUT", "path", "/tmp/render.wav");
engine_start(engine);

// Play notes, then flush and stop
engine_file_writer_flush(engine);
engine_stop(engine);
```

The WAV header is finalised and the file is closed on `engine_destroy` (automatic flush). Use `engine_file_writer_flush` for an explicit intermediate checkpoint while the engine continues running.

**Limitations**: mono/stereo only; no compressed output; last chain node only (SINK role).

### 14.3  AUDIO_INPUT — Hardware Capture Source

`AUDIO_INPUT` is registered as a SOURCE and accepted by `bake()` at the chain head. **It currently outputs silence.** Live routing to actual hardware capture is deferred to Phase 25 (USB/HAL I/O). To prepare a chain that will use hardware input once Phase 25 lands:

```c
engine_add_module(engine, "AUDIO_INPUT", "MIC");
engine_add_module(engine, "MOOG_FILTER", "VCF");
// ...
engine_bake(engine);

// Associate with hardware device (currently a no-op until Phase 25)
engine_open_audio_input(engine, 0);
```

### 14.4  AUDIO_OUTPUT — Explicit Chain Terminator

`AUDIO_OUTPUT` is an optional SINK node that can be added as the last chain entry for patch editors that require an explicit output endpoint. Audio behaviour is identical whether or not it is present.

```c
engine_add_module(engine, "VCA",          "VCA");
engine_add_module(engine, "AUDIO_OUTPUT", "OUT");
engine_connect_ports(engine, "VCA", "audio_out", "OUT", "audio_in");
engine_bake(engine);
```

---

## 15  Known Limitations and Planned Features

| Feature                         | Status                                                                                              |
|---------------------------------|-----------------------------------------------------------------------------------------------------|
| Hard VCO sync                   | Not implemented — designed (ARCH_PLAN.md §Hard VCO Sync); candidate Phase 28                       |
| Band-reject / notch filter      | Not implemented (requires parallel signal path routing)                                             |
| Exponential VCA `response_curve`| **Implemented** — Phase 26; blends linear (0) and exponential (1) gain law                         |
| `initial_gain_cv` port wiring   | **Implemented** — Phase 26; connected port overrides `initial_gain` parameter                      |
| Percussion trill / arpeggiator  | Not implemented (requires sequencer)                                                                |
| Audio-rate FM via `pitch_cv`    | Not possible: `pitch_cv` is PORT_CONTROL; audio-rate sources cannot connect directly                |
| Audio-rate FM via `fm_in`       | **Available** — `fm_in` is PORT_AUDIO on `COMPOSITE_GENERATOR` and all ladder filters; wire `VCO.audio_out → VCF.fm_in` or `VCO1.audio_out → VCO2.fm_in` |
| Multi-VCO additive mixing       | **Available** — `AUDIO_MIXER` registered with 4 audio inputs and per-input gain parameters          |
| Tom tom (Vol. 2 Fig 3-14)       | **Available** — `patches/tom_tom.json` delivered Phase 26 using `fm_in` for FM pitch drop          |
| Gong with noise layer (Fig 3-17)| **Available** — `patches/gong_noise_layer.json` delivered Phase 26                                 |
| Thunder (Fig 3-24)              | **Available** — `patches/thunder.json` delivered Phase 26                                           |
| Group strings multi-VCO         | **Available** — `patches/group_strings.json` delivered Phase 26                                    |
| Vocoder / formant filter        | Not implemented                                                                                     |
| Selective module stripping      | Not yet — CMake presets exist but all 30 modules compile in every preset; future phase              |
| USB MIDI HAL                    | Planned — Phase 25 (`midi_open_input`, `midi_connect_to_engine` C API)                              |
| Multi-timbral SMF playback      | Planned — Phase 23 (MIDI channel → VoiceGroup routing table)                                        |
| `MIDI_CV` source module         | **Planned — Phase 27E**; routable `pitch_cv`, `gate_cv`, `velocity_cv`, `aftertouch_cv` output ports; replaces implicit lifecycle callbacks |
| Two-port pitch on `COMPOSITE_GENERATOR` | **Planned — Phase 27E**; `pitch_base_cv` (absolute, from `MIDI_CV`) + `pitch_cv` (mod offset, from LFO/portamento) sum at oscillator |
| Gate-only trigger model         | **Planned — Phase 27E**; `engine_note_on/off` drive `MIDI_CV` state; `on_note_on/off` callbacks removed from public API |
| `LFO.control_out_inv` port      | **Implemented — Phase 27E**; inverted output port (−1 × `control_out`); eliminates `INVERTER` for counter-phase routing |
| `CV_MIXER.cv_out_inv` port      | **Implemented — Phase 27E**; inverted sum output (−1 × `cv_out`); M-132 INV OUT precedent; counter-phase routing without a downstream `INVERTER` |
| Roland M-132 gate-bias pattern  | **Planned — Phase 27E**; `MIDI_CV.gate_cv` → `CV_MIXER` + `offset` bias replicates M-132 gated LFO trigger (banjo Fig 3-4 canonical wiring); see BRIDGE_GUIDE.md §15.5 |
| `ECHO_DELAY.time_cv` port       | **Implemented — Phase 27E**; additive delay-time CV modulation (seconds); M-172 BBD clock CV precedent |
| `GATE_DELAY` enhancements       | **Implemented — Phase 27E**; `gate_in_b` OR input, `gate_time` fixed-pulse parameter, `delay_time` extended to 6 s |
| Patch audit — Phase 27E migration | **Deferred**; 29 patches reviewed against Roland service notes after Phase 27E implementation; 10 patches verified in arch-audit (2026-03-21) |
| Module introspection API        | **Implemented** — Phase 27A (`module_get_descriptor_json`, `module_registry_get_all_json`)           |
| Patch serialization             | **Implemented** — Phase 27B (`engine_load_patch_json`, `engine_get_patch_json`, `engine_save_patch`); patch format v3 adds top-level `post_chain` array for global effects |
| Role classification             | **Implemented** — Phase 27C; `"role"` field in every module's JSON descriptor (`SOURCE`, `SINK`, `PROCESSOR`); pure CV modules (`LFO`, `ADSR_ENVELOPE`, etc.) and `COMPOSITE_GENERATOR` are `PROCESSOR` |
| `AUDIO_OUTPUT` chain terminator | **Implemented** — Phase 27C; explicit SINK node accepted as last chain node by `bake()` |
| `AUDIO_INPUT` hardware source   | **Partially implemented** — Phase 27C; module registered and accepted by `bake()`; currently outputs silence; live HAL routing deferred to Phase 25 |
| `AUDIO_FILE_READER` playback    | **Implemented** — Phase 27C; WAV/AIFF file loaded into memory, SRC via libsamplerate; mono/stereo only; no compressed formats (MP3, AAC, OGG); path set via `engine_set_tag_string_param` |
| `AUDIO_FILE_WRITER` recorder    | **Implemented** — Phase 27C; real-time WAV capture to disk; auto-flush on `engine_destroy`; path set via `engine_set_tag_string_param`; mono/stereo only |

---

## 16  Quick Reference — Parameter Names by Module

| Module               | Parameters                                                                      |
|----------------------|---------------------------------------------------------------------------------|
| `COMPOSITE_GENERATOR`| `saw_gain`, `pulse_gain`, `sub_gain`, `sine_gain`, `triangle_gain`, `noise_gain`, `detune` (cents), `transpose` (semitones), `pulse_width` [0, 0.5] |
| `LFO`                | `rate` (Hz), `intensity` (semitones), `waveform`                                |
| `MATHS`              | `rise` (s), `fall` (s), `curve` (0=linear, 1=exponential)                      |
| `MOOG_FILTER`        | `cutoff` (Hz, log), `resonance` [0, 1]                                          |
| `DIODE_FILTER`       | `cutoff` (Hz, log), `resonance` [0, 1], `env_depth` [0, 6]                      |
| `SH_FILTER`          | `cutoff` (Hz, log), `resonance` [0, 1]                                          |
| `MS20_FILTER`        | `cutoff` (Hz, log), `resonance` [0, 1]                                          |
| `HIGH_PASS_FILTER`   | `cutoff` (Hz)                                                                   |
| `BAND_PASS_FILTER`   | `cutoff` (Hz), `bandwidth` (Hz)                                                 |
| `ADSR_ENVELOPE`      | `attack`, `decay`, `release` (s); `sustain` [0, 1]                             |
| `AD_ENVELOPE`        | `attack`, `decay` (s)                                                           |
| `VCA`                | `initial_gain` [0, 1], `response_curve` [0, 1]                                  |
| `RING_MOD`           | `mix` [0, 1]                                                                    |
| `DRAWBAR_ORGAN`      | `drawbar_16`, `drawbar_8`, `drawbar_4`, `drawbar_2`, `drawbar_1` (0–8)          |
| `WHITE_NOISE`        | (none)                                                                          |
| `ECHO_DELAY`         | `delay_ms`, `feedback` [0, 1], `mix` [0, 1]                                    |
| `REVERB_FREEVERB`    | `room_size` [0, 1], `damping` [0, 1], `mix` [0, 1]                             |
| `REVERB_FDN`         | `rt60` (s), `mix` [0, 1]                                                        |
| `PHASER`             | `rate` (Hz), `depth` [0, 1], `feedback` [0, 1], `mix` [0, 1]                   |
| `JUNO_CHORUS`        | `rate` (Hz), `depth` [0, 1], `mix` [0, 1]                                      |
| `DISTORTION`         | `drive` [1, 40], `character` [0, 1]                                             |
| `NOISE_GATE`         | `threshold` [0, 1], `attack` (s), `release` (s)                                |
| `ENVELOPE_FOLLOWER`  | `attack` (s), `release` (s)                                                     |
| `CV_MIXER`           | `gain_1`–`gain_4` [-1, 1], `offset` [-1, 1]; ports: `cv_in_1`–`cv_in_4`, `cv_out`, `cv_out_inv` (always −1 × `cv_out`; Phase 27E) |
| `CV_SPLITTER`        | `gain_1`–`gain_4` [0, 1]; ports: `cv_in`, `cv_out_1`–`cv_out_4`; all outputs share one buffer — only `gain_1` applied by executor; use downstream `CV_SCALER` for per-branch depth |
| `CV_SCALER`          | `gain` [-4, 4] (default 1.0); ports: `cv_in`, `cv_out`; scales a CV signal by an arbitrary factor (attenuator, inverter, or amplifier) |
| `SAMPLE_HOLD`        | (none — driven by clock_in port)                                                |
| `GATE_DELAY`         | `delay_ms`                                                                      |
| `INVERTER`           | `scale` (default −1.0)                                                          |
| `AUDIO_SPLITTER`     | `gain_0`–`gain_3` [0, 1]                                                        |
| `AUDIO_OUTPUT`       | (none — transparent SINK passthrough)                                           |
| `AUDIO_INPUT`        | `device_index` (0–16, default 0), `gain` [0, 4]                                 |
| `AUDIO_FILE_READER`  | `loop` (0/1, default 0), `gain` [0, 4]; string param: `path` (via `engine_set_tag_string_param`) |
| `AUDIO_FILE_WRITER`  | `max_seconds` [0, 86400], `max_file_mb` [0, 4096]; string param: `path` (via `engine_set_tag_string_param`) |

---

*Cross-referenced against Roland Corporation "A Foundation for Electronic Music" 2nd Ed. (1978),
R. D. Graham "Practical Synthesis for Electronic Music, Vol. 1" (1979),
R. D. Graham "Practical Synthesis for Electronic Music, Vol. 2" (2nd Ed.),
ARCH_PLAN.md, MODULE_DESC.md, and PATCH_SPEC.md.*
