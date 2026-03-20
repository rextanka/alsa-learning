#!/usr/bin/env python3
"""
configure_modules.py — module configuration helper and patch validator.

Usage
-----
  # List all available modules
  python tools/configure_modules.py --list

  # Show which modules a preset includes and validate patches against it
  python tools/configure_modules.py --preset pi_minimal
  python tools/configure_modules.py --preset pi_synth

  # Validate every patch in patches/ against the full module set
  python tools/configure_modules.py --validate-patches

  # Validate patches against a specific module set
  python tools/configure_modules.py --preset pi_synth --validate-patches

  # Interactive: select modules, then validate patches
  python tools/configure_modules.py --interactive
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Module catalogue
# Each entry: (description, category)
# ---------------------------------------------------------------------------

MODULES: dict[str, tuple[str, str]] = {
    # Oscillators / generators
    "COMPOSITE_GENERATOR": (
        "Multi-waveform VCO: sawtooth, pulse, sub, sine, triangle, wavetable, noise",
        "oscillator",
    ),
    "WHITE_NOISE": (
        "LCG-based white noise generator",
        "oscillator",
    ),
    "LFO": (
        "Low-frequency oscillator (sine/triangle/square/saw)",
        "oscillator",
    ),
    "DRAWBAR_ORGAN": (
        "9-partial tonewheel organ at Hammond footage ratios",
        "oscillator",
    ),
    # Envelopes
    "ADSR_ENVELOPE": (
        "4-stage ADSR envelope generator (exponential IIR curves)",
        "envelope",
    ),
    "AD_ENVELOPE": (
        "Attack-Decay envelope for percussive sounds — completes Decay regardless of gate",
        "envelope",
    ),
    # Filters
    "MOOG_FILTER": (
        "4-pole Moog transistor ladder LP (24 dB/oct)",
        "filter",
    ),
    "DIODE_FILTER": (
        "TB-style diode ladder LP — 18–24 dB/oct acid character",
        "filter",
    ),
    "SH_FILTER": (
        "SH/CEM/IR3109 4-pole ladder LP (24 dB/oct) — clean, liquid",
        "filter",
    ),
    "MS20_FILTER": (
        "MS-20 dual 2-pole HP+LP SVF (12 dB/oct each) — aggressive",
        "filter",
    ),
    "HIGH_PASS_FILTER": (
        "2-pole biquad high-pass filter",
        "filter",
    ),
    "BAND_PASS_FILTER": (
        "2-pole biquad band-pass filter",
        "filter",
    ),
    # Dynamics
    "VCA": (
        "Voltage-controlled amplifier — scales audio by a gain CV signal",
        "dynamics",
    ),
    "NOISE_GATE": (
        "Threshold-based gate: opens above threshold, closes on silence",
        "dynamics",
    ),
    "ENVELOPE_FOLLOWER": (
        "Extracts a dynamic control signal (RMS envelope) from audio input",
        "dynamics",
    ),
    # Routing / utility
    "INVERTER": (
        "CV signal inverter/scaler: cv_out = scale * cv_in",
        "routing",
    ),
    "CV_MIXER": (
        "4-input CV mixer/attenuverter with DC offset",
        "routing",
    ),
    "CV_SPLITTER": (
        "1-to-4 CV fan-out with per-output gain scaling",
        "routing",
    ),
    "MATHS": (
        "Slew limiter / portamento: rise/fall time control",
        "routing",
    ),
    "GATE_DELAY": (
        "Gate delay / pulse shaper: delays note-on gate by a fixed time",
        "routing",
    ),
    "SAMPLE_HOLD": (
        "Sample & Hold: freezes cv_in on each rising clock edge",
        "routing",
    ),
    "RING_MOD": (
        "4-quadrant ring modulator: output = audio_in_a × audio_in_b",
        "routing",
    ),
    "AUDIO_SPLITTER": (
        "1-to-4 audio fan-out with per-output gain",
        "routing",
    ),
    "AUDIO_MIXER": (
        "4-input audio summing mixer with per-input gain",
        "routing",
    ),
    # FX
    "ECHO_DELAY": (
        "Modulated delay line (BBD-style): static delay + LFO shimmer",
        "fx",
    ),
    "PHASER": (
        "4/8-stage all-pass phaser with stereo quadrature LFO",
        "fx",
    ),
    "JUNO_CHORUS": (
        "Roland Juno-60 BBD stereo chorus emulation",
        "fx",
    ),
    "DISTORTION": (
        "Guitar-style distortion with 4x oversampling — drive + character blend",
        "fx",
    ),
    "REVERB_FREEVERB": (
        "Schroeder/Freeverb stereo reverb: 8 combs + 4 all-pass per channel",
        "fx",
    ),
    "REVERB_FDN": (
        "Jean-Marc Jot FDN reverb: 8-line Householder network — high CPU",
        "fx",
    ),
}

# ---------------------------------------------------------------------------
# Named presets (mirror CMakePresets.json intent)
# ---------------------------------------------------------------------------

PRESETS: dict[str, set[str]] = {
    "desktop_full": set(MODULES.keys()),
    "pi_synth":     set(MODULES.keys()),
    "pi_minimal":   set(MODULES.keys()),
}

PRESET_DESCRIPTIONS: dict[str, str] = {
    "desktop_full": "All 30 modules — macOS/Linux desktop development",
    "pi_synth":     "All 30 modules — Raspberry Pi arm64 release build",
    "pi_minimal":   "All 30 modules — Raspberry Pi minimal/embedded release build",
}

CATEGORIES = ["oscillator", "envelope", "filter", "dynamics", "routing", "fx"]


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def modules_by_category(enabled: set[str]) -> dict[str, list[str]]:
    result: dict[str, list[str]] = {c: [] for c in CATEGORIES}
    for name, (_, cat) in MODULES.items():
        if name in enabled:
            result[cat].append(name)
    return result


def collect_patch_types(patch_path: Path) -> set[str]:
    """Return the set of module type strings used in a patch file."""
    with patch_path.open() as f:
        data = json.load(f)
    types: set[str] = set()
    for group in data.get("groups", []):
        for node in group.get("chain", []):
            t = node.get("type")
            if t:
                types.add(t)
    return types


def validate_patches(patch_dir: Path, enabled: set[str]) -> list[tuple[Path, set[str]]]:
    """
    Validate all *.json files in patch_dir against the enabled module set.
    Returns a list of (patch_path, missing_types) for patches that fail.
    """
    failures: list[tuple[Path, set[str]]] = []
    for patch_path in sorted(patch_dir.glob("*.json")):
        try:
            used = collect_patch_types(patch_path)
        except (json.JSONDecodeError, KeyError) as e:
            print(f"  [WARN] Could not parse {patch_path.name}: {e}")
            continue
        missing = used - enabled
        if missing:
            failures.append((patch_path, missing))
    return failures


# ---------------------------------------------------------------------------
# CLI actions
# ---------------------------------------------------------------------------


def cmd_list(_args: argparse.Namespace) -> int:
    print(f"{'Module':<25}  {'Category':<10}  Description")
    print("-" * 80)
    for cat in CATEGORIES:
        for name, (desc, c) in MODULES.items():
            if c == cat:
                print(f"  {name:<23}  {cat:<10}  {desc}")
    print(f"\n{len(MODULES)} modules total.")
    return 0


def cmd_preset(args: argparse.Namespace, patch_dir: Path | None = None) -> int:
    name = args.preset
    if name not in PRESETS:
        print(f"Error: unknown preset '{name}'. Known: {', '.join(PRESETS)}")
        return 1

    enabled = PRESETS[name]
    print(f"Preset: {name}")
    print(f"  {PRESET_DESCRIPTIONS[name]}")
    print(f"  {len(enabled)}/{len(MODULES)} modules enabled\n")

    by_cat = modules_by_category(enabled)
    for cat in CATEGORIES:
        mods = by_cat[cat]
        excluded = [m for m, (_, c) in MODULES.items() if c == cat and m not in enabled]
        print(f"  [{cat}]")
        for m in mods:
            print(f"    + {m}")
        for m in excluded:
            print(f"    - {m}  (excluded)")

    if patch_dir is not None:
        _run_validation(enabled, patch_dir)

    return 0


def cmd_validate(args: argparse.Namespace) -> int:
    patch_dir = Path(getattr(args, "patch_dir", "patches"))
    preset_name = getattr(args, "preset", None)
    if preset_name:
        enabled = PRESETS.get(preset_name, set(MODULES.keys()))
        print(f"Validating against preset '{preset_name}' ({len(enabled)} modules)...\n")
    else:
        enabled = set(MODULES.keys())
        print(f"Validating against full module set ({len(enabled)} modules)...\n")
    _run_validation(enabled, patch_dir)
    return 0


def _run_validation(enabled: set[str], patch_dir: Path) -> None:
    if not patch_dir.exists():
        print(f"  [WARN] Patch directory not found: {patch_dir}")
        return

    all_patches = sorted(patch_dir.glob("*.json"))
    if not all_patches:
        print(f"  No patches found in {patch_dir}")
        return

    failures = validate_patches(patch_dir, enabled)
    ok = len(all_patches) - len(failures)

    print(f"  Patches scanned : {len(all_patches)}")
    print(f"  Passed          : {ok}")
    print(f"  Failed          : {len(failures)}")

    if failures:
        print()
        for patch_path, missing in failures:
            print(f"  FAIL  {patch_path.name}")
            for m in sorted(missing):
                desc = MODULES.get(m, ("unknown",))[0]
                print(f"        missing: {m}  ({desc})")
    else:
        print("\n  All patches are compatible with the selected module set.")


def cmd_interactive(_args: argparse.Namespace) -> int:
    print("Interactive module configuration")
    print("=" * 50)
    print("\nChoose a starting point:")
    for i, (pname, pdesc) in enumerate(PRESET_DESCRIPTIONS.items(), 1):
        print(f"  {i}) {pname:<20}  {pdesc}")
    print(f"  {len(PRESETS) + 1}) custom — start from scratch")
    print()

    choice = _prompt_int("Starting point", 1, len(PRESETS) + 1)
    preset_names = list(PRESETS.keys())

    if choice <= len(PRESETS):
        enabled = set(PRESETS[preset_names[choice - 1]])
        print(f"\nStarting from preset '{preset_names[choice - 1]}' ({len(enabled)} modules).\n")
    else:
        enabled = set()
        print("\nStarting from scratch.\n")

    # Per-category toggle
    for cat in CATEGORIES:
        cat_mods = [m for m, (_, c) in MODULES.items() if c == cat]
        enabled_in_cat = [m for m in cat_mods if m in enabled]
        disabled_in_cat = [m for m in cat_mods if m not in enabled]

        print(f"  [{cat.upper()}]  {len(enabled_in_cat)}/{len(cat_mods)} enabled")
        for m in cat_mods:
            state = "ON " if m in enabled else "OFF"
            desc, _ = MODULES[m]
            print(f"    [{state}] {m:<25}  {desc}")

        ans = input(f"\n  Toggle any modules in '{cat}'? (comma-separated names, or Enter to skip): ").strip()
        if ans:
            for token in ans.split(","):
                t = token.strip().upper()
                if t in {m for m in cat_mods}:
                    if t in enabled:
                        enabled.discard(t)
                        print(f"    Disabled: {t}")
                    else:
                        enabled.add(t)
                        print(f"    Enabled:  {t}")
                else:
                    print(f"    Unknown:  '{t}' — skipped")
        print()

    print("\nFinal configuration: %d/%d modules enabled" % (len(enabled), len(MODULES)))
    by_cat = modules_by_category(enabled)
    for cat in CATEGORIES:
        mods = by_cat[cat]
        if mods:
            print(f"  [{cat}] {', '.join(mods)}")

    # Offer patch validation
    patch_dir = Path("patches")
    if patch_dir.exists():
        ans = input("\nValidate patches against this module set? [Y/n]: ").strip().lower()
        if ans in ("", "y", "yes"):
            print()
            _run_validation(enabled, patch_dir)

    return 0


def _prompt_int(prompt: str, lo: int, hi: int) -> int:
    while True:
        raw = input(f"{prompt} [{lo}-{hi}]: ").strip()
        try:
            v = int(raw)
            if lo <= v <= hi:
                return v
        except ValueError:
            pass
        print(f"  Please enter a number between {lo} and {hi}.")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    default_patch_dir = repo_root / "patches"

    parser = argparse.ArgumentParser(
        description="Audio engine module configuration and patch validator.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    sub = parser.add_subparsers(dest="command")

    sub.add_parser("list", help="List all available modules")

    p_preset = sub.add_parser("preset", help="Show a named preset and optionally validate patches")
    p_preset.add_argument("preset", choices=list(PRESETS.keys()), help="Preset name")
    p_preset.add_argument(
        "--validate-patches",
        nargs="?",
        const=str(default_patch_dir),
        metavar="DIR",
        help=f"Validate patches (default: {default_patch_dir})",
    )

    p_val = sub.add_parser("validate", help="Validate patches against a module set")
    p_val.add_argument(
        "patch_dir",
        nargs="?",
        default=str(default_patch_dir),
        help=f"Patch directory (default: {default_patch_dir})",
    )
    p_val.add_argument(
        "--preset",
        choices=list(PRESETS.keys()),
        help="Restrict to a named preset (default: full set)",
    )

    sub.add_parser("interactive", help="Interactive module selection + patch validation")

    args = parser.parse_args()

    if args.command == "list":
        return cmd_list(args)
    elif args.command == "preset":
        patch_dir = Path(args.validate_patches) if args.validate_patches else None
        return cmd_preset(args, patch_dir)
    elif args.command == "validate":
        return cmd_validate(args)
    elif args.command == "interactive":
        return cmd_interactive(args)
    else:
        parser.print_help()
        return 0


if __name__ == "__main__":
    sys.exit(main())
