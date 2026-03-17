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
`DIODE_FILTER`, `LFO`, `DRAWBAR_ORGAN`. See MODULE_DESC.md for the full registry.

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

Key–value pairs using the string labels defined in BRIDGE_GUIDE.md §3 and MODULE_DESC.md.

```json
"parameters": {
  "sine_gain":   1.0,
  "amp_attack":  0.005,
  "amp_decay":   0.15,
  "amp_sustain": 0.4,
  "amp_release": 0.1,
  "vcf_cutoff":  450.0,
  "vcf_res":     0.6
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
        "pulse_gain":   0.8,
        "sub_gain":     0.6,
        "noise_gain":   0.1,
        "amp_attack":   0.005,
        "amp_decay":    0.15,
        "amp_sustain":  0.4,
        "amp_release":  0.1
      }
    }
  ]
}
```

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
