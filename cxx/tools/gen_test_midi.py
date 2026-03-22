#!/usr/bin/env python3
"""
gen_test_midi.py — generate patch test MIDI fixtures.

Each patch in patches/ gets a companion .mid file in midi/ that plays a
musically appropriate sequence at the right tempo and articulation.  The
SMF files are Format-0, single track, 480 PPQN throughout.

Usage:
    python3 tools/gen_test_midi.py [--out-dir midi]

The script is the authoritative source of musical content for every patch
test.  Edit the PATCHES list below to change what a test plays.
"""

import argparse
import os
import struct

# ---------------------------------------------------------------------------
# SMF primitives
# ---------------------------------------------------------------------------

def var_len(n: int) -> bytes:
    """Encode a non-negative integer as a MIDI variable-length quantity."""
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))


def tempo_event(bpm: int) -> bytes:
    us = 60_000_000 // bpm
    return b'\x00\xff\x51\x03' + struct.pack('>I', us)[1:]  # 3-byte µs field


def time_sig_event(num: int, denom_pow2: int) -> bytes:
    """FF 58 04 nn dd cc bb — time signature meta event."""
    return b'\x00\xff\x58\x04' + bytes([num, denom_pow2, 24, 8])


def note_on(delta: int, note: int, vel: int, ch: int = 0) -> bytes:
    return var_len(delta) + bytes([0x90 | ch, note, vel])


def note_off(delta: int, note: int, ch: int = 0) -> bytes:
    return var_len(delta) + bytes([0x80 | ch, note, 0])


def end_of_track() -> bytes:
    return b'\x00\xff\x2f\x00'


def make_smf(ppqn: int, track_events: bytes) -> bytes:
    track = track_events + end_of_track()
    header = b'MThd' + struct.pack('>IHHH', 6, 0, 1, ppqn)
    chunk  = b'MTrk' + struct.pack('>I', len(track)) + track
    return header + chunk


def write_smf(path: str, ppqn: int, events: bytes) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(make_smf(ppqn, events))
    print(f"  wrote {path}")


# ---------------------------------------------------------------------------
# Musical helpers — all durations in ticks at 480 PPQN
# ---------------------------------------------------------------------------

Q  = 480      # quarter note
H  = 960      # half note
W  = 1920     # whole note
E  = 240      # eighth
S  = 120      # sixteenth
DQ = 720      # dotted quarter
DE = 360      # dotted eighth

# MIDI note numbers
def note(name: str) -> int:
    names = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
    octave = int(name[-1])
    acc = name[1:-1]  # '' | '#' | 'b'
    base = names[name[0]] + (1 if acc == '#' else -1 if acc == 'b' else 0)
    return (octave + 1) * 12 + base


def seq(notes_durs: list, vel: int = 90, gate: float = 0.85) -> bytes:
    """
    Build a monophonic sequence.
    notes_durs: list of (note_number, duration_ticks) or (None, duration) for rest.
    gate: note-on duration as fraction of step duration.

    Rests are absorbed into the following note_on's delta — never emitted as
    standalone delta bytes (which are invalid SMF).
    """
    out = b''
    pending = 0  # accumulated delta ticks from rests / inter-note gaps
    for (n, dur) in notes_durs:
        on_dur  = max(1, int(dur * gate))
        off_dur = dur - on_dur
        if n is None:
            pending += dur           # absorb rest into next note's delta
        else:
            out += note_on(pending, n, vel) + note_off(on_dur, n)
            pending = off_dur        # carry inter-note gap to next note's delta
    # Trailing pending (after last note) is silently dropped — end_of_track follows
    return out


def chord_on(delta: int, notes: list, vel: int) -> bytes:
    out = var_len(delta) + bytes([0x90, notes[0], vel])
    for n in notes[1:]:
        out += bytes([0x00, 0x90, n, vel])
    return out


def chord_off(delta: int, notes: list) -> bytes:
    out = var_len(delta) + bytes([0x80, notes[0], 0])
    for n in notes[1:]:
        out += bytes([0x00, 0x80, n, 0])
    return out


def hold(n: int, dur: int, vel: int = 90, gate: float = 0.92) -> bytes:
    """Single note with gate, delta=0. No trailing delta — callers absorb the gap."""
    on_ticks = max(1, int(dur * gate))
    return note_on(0, n, vel) + note_off(on_ticks, n)


# ---------------------------------------------------------------------------
# Per-patch MIDI content definitions
# ---------------------------------------------------------------------------

PPQN = 480

def mk_acid_reverb() -> bytes:
    """TB-303 acid riff — 16 steps at 130 BPM, classic semitone slides."""
    t = tempo_event(130) + time_sig_event(4, 2)
    # Pattern: C2 C2 G1 Bb1 C2 C2 G1 Ab1 (one bar, 16th notes, accented 1+3)
    pat = [
        (note('C2'), S, 100), (note('C2'), S, 70), (note('G1'), S, 95),
        (note('A#1'), S, 70), (note('C2'), S, 100), (note('C2'), S, 70),
        (note('G1'), S, 95),  (note('A#1'), S, 70), (note('C2'), S, 100),
        (note('C2'), S, 70),  (note('G1'), S, 95),  (note('A#1'), S, 70),
        (note('C2'), S, 100), (note('C2'), S, 70),  (note('G1'), S, 95),
        (note('A#1'), S, 70),
    ]
    notes = b''
    for (n, dur, v) in pat:
        on = max(1, int(dur * 0.75))
        notes += note_on(0, n, v) + note_off(on, n) + var_len(dur - on)
    # Two bars
    return t + notes + notes


def mk_banjo() -> bytes:
    """Banjo — 4-bar bluegrass run in G, 120 BPM, mainly 8th notes.
    Bar 1: driving G major ascending/descending pick run (8 eighths).
    Bar 2: two 8th pickups then a half note held on D4 (beats 2–4), 8th tag.
    Bar 3: pentatonic descent run, syncopated (8 eighths).
    Bar 4: closing cadence figure resolving to G3 (8 eighths).
    Gate 55% for crisp strum separation; LFO strums each held note.
    """
    t = tempo_event(120) + time_sig_event(4, 2)
    GATE = 0.35
    G  = note('G3');  B  = note('B3');  D4 = note('D4')
    E4 = note('E4');  G4 = note('G4');  D3 = note('D3')
    B2 = note('B2');  A3 = note('A3');  C4 = note('C4')

    def pick(n, dur, vel=100):
        on = max(1, int(dur * GATE))
        return note_on(0, n, vel) + note_off(on, n) + var_len(dur - on)

    # Bar 1: G3 B3 D4 G4 D4 B3 D3 G3  (ascending then descending, all 8ths)
    bar1 = (pick(G,  E, 100) + pick(B,  E, 95) + pick(D4, E, 100) + pick(G4, E, 105) +
            pick(D4, E,  95) + pick(B,  E, 95) + pick(D3, E,  90) + pick(G,  E,  95))

    # Bar 2: G3 B3 | D4 (half note, held — LFO strums it) | G3 (8th tag)
    # beats: 1-e  2-e  3-e-4-e           4+
    bar2 = (pick(G,  E, 100) + pick(B,  E, 95) +   # beat 1 (2 eighths)
            # D4 half note: on immediately, off after 2 beats
            note_on(0, D4, 105) + note_off(H, D4) +
            pick(G, E, 90))                          # 8th tag on beat 4+

    # Bar 3: pentatonic descent A3 G3 E4 D4 B3 G3 D3 B2 (bluegrass pull-off feel)
    bar3 = (pick(A3, E, 100) + pick(G,  E, 95) + pick(E4, E, 100) + pick(D4, E, 95) +
            pick(B,  E,  95) + pick(G,  E, 90) + pick(D3, E,  90) + pick(B2, E, 85))

    # Bar 4: closing — D3 G3 B3 D4 G4 D4 B3 G3 (resolve home)
    bar4 = (pick(D3, E,  90) + pick(G,  E, 95) + pick(B,  E, 100) + pick(D4, E, 105) +
            pick(G4, E, 100) + pick(D4, E, 95) + pick(B,  E,  95) + pick(G,  E, 100))

    return t + bar1 + bar2 + bar3 + bar4


def mk_bass_drum() -> bytes:
    """Four-on-the-floor kick at 120 BPM, 2 bars (C1 = standard GM kick)."""
    t = tempo_event(120) + time_sig_event(4, 2)
    kick = note('C1')
    # Beat 1 2 3 4 | beat 1 2 3 4  (quarter notes, short gate)
    bar = b''
    for _ in range(8):
        bar += note_on(0, kick, 110) + note_off(int(Q * 0.15), kick) + var_len(Q - int(Q * 0.15))
    return t + bar


def mk_bell() -> bytes:
    """Bell tones via ring mod — C4 G4, each struck and left to decay. 100 BPM, 2 bars."""
    t = tempo_event(100) + time_sig_event(4, 2)
    strikes = [(note('C4'), H), (note('G4'), H), (note('E5'), Q), (note('C4'), Q)]
    return t + seq(strikes, vel=100, gate=0.10)   # very short gate, ring sustains


def mk_bongo_drums() -> bytes:
    """Bongo pattern — C3 (low) and G3 (high) in syncopated Latin feel, 120 BPM."""
    t = tempo_event(120) + time_sig_event(4, 2)
    lo, hi = note('C3'), note('G3')
    # 1e+2e+3e+4e+ syncopated: lo hi lo hi hi lo hi lo (eighth notes)
    pat = [(lo, E, 100), (hi, E, 85), (lo, E, 95), (hi, E, 80),
           (hi, E, 90), (lo, E, 100),(hi, E, 85), (lo, E, 90)]
    notes = b''
    for (n, dur, v) in pat:
        on = max(1, int(dur * 0.55))
        notes += note_on(0, n, v) + note_off(on, n) + var_len(dur - on)
    return t + notes + notes


def mk_bowed_bass() -> bytes:
    """Bowed bass — walking line in C minor. 100 BPM, 2 bars."""
    t = tempo_event(100) + time_sig_event(4, 2)
    walk = [(note('C2'), Q), (note('Eb2'), Q), (note('G2'), Q), (note('F2'), Q),
            (note('C2'), Q), (note('Bb1'), Q), (note('Ab1'), Q), (note('G1'), Q)]
    return t + seq(walk, vel=85, gate=0.92)


def mk_brass() -> bytes:
    """Brass stabs — short punchy notes, C major fanfare at 110 BPM."""
    t = tempo_event(110) + time_sig_event(4, 2)
    fanfare = [(note('C4'), E), (note('E4'), E), (note('G4'), E), (note('C5'), Q),
               (None, E),
               (note('C5'), E), (note('G4'), E), (note('E4'), E), (note('C4'), H)]
    return t + seq(fanfare, vel=100, gate=0.65)


def mk_clarinet() -> bytes:
    """Clarinet phrase — smooth legato in G major, 100 BPM, 2 bars."""
    t = tempo_event(100) + time_sig_event(4, 2)
    phrase = [(note('G4'), Q), (note('A4'), Q), (note('B4'), H),
              (note('D5'), Q), (note('C5'), Q), (note('B4'), Q), (note('A4'), Q),
              (note('G4'), H)]
    return t + seq(phrase, vel=80, gate=0.95)


def mk_cow_bell() -> bytes:
    """Cowbell — steady eighth notes A3 at 120 BPM (disco pattern)."""
    t = tempo_event(120) + time_sig_event(4, 2)
    cb = note('A3')
    bar = b''
    for i in range(16):  # 2 bars of eighths
        v = 100 if i % 2 == 0 else 75
        bar += note_on(0, cb, v) + note_off(int(E * 0.4), cb) + var_len(E - int(E * 0.4))
    return t + bar


def mk_cymbal() -> bytes:
    """Cymbal shimmer — single strike C4, let the BBD modulation ring out. 100 BPM, 1 bar."""
    t = tempo_event(100) + time_sig_event(4, 2)
    return t + note_on(0, note('C4'), 100) + note_off(W, note('C4'))


def mk_delay_lead() -> bytes:
    """Delay lead — sparse melody so echoes are audible, 100 BPM."""
    t = tempo_event(100) + time_sig_event(4, 2)
    # Dotted-eighth delay at 100 BPM = 360 ms.  Notes spaced a bar apart let tails overlap.
    phrase = [(note('C4'), DQ), (None, DQ),
              (note('E4'), DQ), (None, DQ),
              (note('G4'), Q),  (None, Q),
              (note('E4'), DQ), (None, DQ),
              (note('C4'), W)]
    return t + seq(phrase, vel=90, gate=0.25)   # short gate, delay carries sustain


def mk_dog_whistle() -> bytes:
    """Dog whistle — two short high notes with rests, 100 BPM."""
    t = tempo_event(100) + time_sig_event(4, 2)
    phrase = [(note('C6'), E), (None, H), (note('G6'), E)]
    return t + seq(phrase, vel=85, gate=0.20)


def mk_english_horn() -> bytes:
    """English horn — lyrical phrase in D minor, 100 BPM, 2 bars."""
    t = tempo_event(100) + time_sig_event(4, 2)
    phrase = [(note('D4'), H), (note('F4'), Q), (note('E4'), Q),
              (note('D4'), Q), (note('C4'), Q), (note('A3'), H)]
    return t + seq(phrase, vel=78, gate=0.95)


def mk_flute() -> bytes:
    """Flute — light articulated phrase in C major, 100 BPM."""
    t = tempo_event(100) + time_sig_event(4, 2)
    phrase = [(note('C5'), E), (note('D5'), E), (note('E5'), Q),
              (note('G5'), E), (note('F5'), E), (note('E5'), Q),
              (note('D5'), E), (note('C5'), E), (note('E5'), H)]
    return t + seq(phrase, vel=72, gate=0.88)


def mk_glockenspiel() -> bytes:
    """Glockenspiel — simple twinkle melody at 108 BPM."""
    t = tempo_event(108) + time_sig_event(4, 2)
    twinkle = [(note('C5'), Q), (note('C5'), Q), (note('G5'), Q), (note('G5'), Q),
               (note('A5'), Q), (note('A5'), Q), (note('G5'), H),
               (note('F5'), Q), (note('F5'), Q), (note('E5'), Q), (note('E5'), Q),
               (note('D5'), Q), (note('D5'), Q), (note('C5'), H)]
    return t + seq(twinkle, vel=88, gate=0.55)   # short gate = bell-like decay


def mk_gong_full() -> bytes:
    """Gong (full) — single heavy strike, decay tail. 100 BPM, 1 bar."""
    t = tempo_event(100) + time_sig_event(4, 2)
    return t + note_on(0, note('C2'), 110) + note_off(W, note('C2'))


def mk_gong() -> bytes:
    """Gong — single C3 strike, shimmer decay. 100 BPM, 1 bar."""
    t = tempo_event(100) + time_sig_event(4, 2)
    return t + note_on(0, note('C3'), 100) + note_off(W, note('C3'))


def mk_group_strings() -> bytes:
    """Group strings — G major chord, ensemble shimmer, 100 BPM, 2 bars."""
    t = tempo_event(100) + time_sig_event(4, 2)
    g_maj = [note('G3'), note('B3'), note('D4'), note('G4')]
    return (t
            + chord_on(0, g_maj, 80)
            + chord_off(W + H, g_maj))


def mk_harpsichord() -> bytes:
    """Harpsichord — Baroque-style descending phrase, 100 BPM."""
    t = tempo_event(100) + time_sig_event(4, 2)
    phrase = [(note('G5'), E), (note('F#5'), E), (note('E5'), E), (note('D5'), E),
              (note('C5'), E), (note('B4'), E), (note('A4'), E), (note('G4'), Q),
              (note('A4'), E), (note('B4'), E), (note('C5'), E), (note('D5'), E),
              (note('G4'), H)]
    return t + seq(phrase, vel=92, gate=0.70)


def mk_juno_pad() -> bytes:
    """Juno pad — C major chord with long attack, 100 BPM, 2 bars."""
    t = tempo_event(100) + time_sig_event(4, 2)
    c_maj = [note('C4'), note('E4'), note('G4')]
    return (t
            + chord_on(0, c_maj, 75)
            + chord_off(W + H, c_maj))


def mk_juno_strings() -> bytes:
    """Juno strings — arpeggiated G major, 100 BPM, 2 bars."""
    t = tempo_event(100) + time_sig_event(4, 2)
    arp = [(note('G3'), Q), (note('B3'), Q), (note('D4'), Q), (note('G4'), Q),
           (note('D4'), Q), (note('B3'), Q), (note('G3'), H)]
    return t + seq(arp, vel=82, gate=0.90)


def mk_organ_drawbar() -> bytes:
    """Drawbar organ — four-voice chord progression, 100 BPM, 2 bars."""
    t = tempo_event(100) + time_sig_event(4, 2)
    # Simple walking chord progression: C — F — G — C
    chords = [
        ([note('C3'), note('G3'), note('E4'), note('C5')], H),
        ([note('F3'), note('A3'), note('F4'), note('C5')], H),
        ([note('G3'), note('B3'), note('G4'), note('D5')], H),
        ([note('C3'), note('G3'), note('E4'), note('C5')], H),
    ]
    out = t
    for (ns, dur) in chords:
        on = max(1, int(dur * 0.95))
        out += chord_on(0, ns, 85) + chord_off(on, ns) + var_len(dur - on)
    return out


def mk_percussion_noise() -> bytes:
    """Percussion noise — steady quarter-note hits at 120 BPM."""
    t = tempo_event(120) + time_sig_event(4, 2)
    hit = note('C3')
    bar = b''
    for _ in range(8):
        bar += note_on(0, hit, 100) + note_off(int(Q * 0.12), hit) + var_len(Q - int(Q * 0.12))
    return t + bar


def mk_pizzicato_violin() -> bytes:
    """Pizzicato violin — short plucked notes, Baroque dance at 100 BPM."""
    t = tempo_event(100) + time_sig_event(3, 2)   # 3/4
    phrase = [(note('G4'), Q), (note('D4'), Q), (note('B3'), Q),
              (note('A3'), Q), (note('F#3'), Q),(note('G3'), Q),
              (note('G4'), H), (note('G4'), Q)]
    return t + seq(phrase, vel=88, gate=0.35)   # very short pluck


def mk_rain() -> bytes:
    """Rain — sustained noise wash, 100 BPM, 2 bars."""
    t = tempo_event(100) + time_sig_event(4, 2)
    # Single long note (C3 used as trigger; WHITE_NOISE patch ignores pitch)
    return (t
            + note_on(0, note('C3'), 80)
            + note_off(W * 2, note('C3')))


def mk_reverb_pad() -> bytes:
    """Reverb pad — C major chord into reverb tail, 100 BPM, 2 bars."""
    t = tempo_event(100) + time_sig_event(4, 2)
    c_maj = [note('C4'), note('E4'), note('G4')]
    return (t
            + chord_on(0, c_maj, 80)
            + chord_off(W * 2, c_maj))   # hold 2 bars; smoke tail adds 0.5s extra


def mk_sh_bass() -> bytes:
    """SH-101 bass — driving monophonic line in C minor, 110 BPM, 1 bar."""
    t = tempo_event(110) + time_sig_event(4, 2)
    line = [(note('C2'), E), (note('C2'), S), (note('Eb2'), S),
            (note('G2'), E), (note('F2'), E),
            (note('C2'), E), (note('C2'), S), (note('Bb1'), S),
            (note('C2'), Q), (None, Q)]
    return t + seq(line, vel=95, gate=0.80)


def mk_snare_drum() -> bytes:
    """Snare on beats 2+4 at 120 BPM, 2 bars (D1 = GM snare)."""
    t = tempo_event(120) + time_sig_event(4, 2)
    sd = note('D1')
    on_dur = int(Q * 0.12)   # ~57 ticks
    gap    = Q - on_dur       # 423 ticks — gap completing one beat after note_off
    # First snare: delta=Q (one rest beat); subsequent snares: delta=gap
    hit1 = note_on(Q,   sd, 105) + note_off(on_dur, sd)
    hitn = note_on(gap, sd, 105) + note_off(on_dur, sd)
    return t + hit1 + hitn + hitn + hitn  # 4 hits = beats 2,4,6,8 across 2 bars


def mk_strings_chorus_reverb() -> bytes:
    """Strings + post-chain — G major chord for chorus/reverb analysis, 100 BPM, 2 bars."""
    t = tempo_event(100) + time_sig_event(4, 2)
    g_maj = [note('G3'), note('B3'), note('D4')]
    return (t
            + chord_on(0, g_maj, 78)
            + chord_off(W * 2, g_maj))   # hold 2 bars


def mk_tb_bass() -> bytes:
    """TB-303 bass — classic acid pattern in C, 125 BPM, 2 bars."""
    t = tempo_event(125) + time_sig_event(4, 2)
    # Sliding acid line: C2 C2 G1 Bb1 C2 C2 Eb2 D2 (16th notes, varied accents)
    pat = [
        (note('C2'), S, 105), (note('C2'), S, 70), (note('G1'),  S, 95),
        (note('A#1'), S, 75),(note('C2'), S, 100), (note('C2'), S, 70),
        (note('D#2'), S, 90),(note('D2'), S, 80),
    ]
    notes = b''
    for (n, dur, v) in pat:
        on = max(1, int(dur * 0.65))
        notes += note_on(0, n, v) + note_off(on, n) + var_len(dur - on)
    return t + notes + notes   # 2 bars


def mk_thunder() -> bytes:
    """Thunder — single deep low strike with noise decay. 100 BPM, 1 bar."""
    t = tempo_event(100) + time_sig_event(4, 2)
    return (t
            + note_on(0, note('C1'), 120)
            + note_off(W, note('C1')))


def mk_tom_tom() -> bytes:
    """Tom tom fill — C3/E3 alternating at 100 BPM eighth notes."""
    t = tempo_event(100) + time_sig_event(4, 2)
    fill = [note('C3'), note('E3'), note('C3'), note('E3'),
            note('C3'), note('C3'), note('E3'), note('E3')]
    notes = b''
    for n in fill:
        on = max(1, int(E * 0.20))
        notes += note_on(0, n, 90) + note_off(on, n) + var_len(E - on)
    return t + notes


def mk_violin_vibrato() -> bytes:
    """Violin with vibrato — expressive phrase in D major, 100 BPM, 2 bars."""
    t = tempo_event(100) + time_sig_event(4, 2)
    phrase = [(note('D4'), H), (note('F#4'), Q), (note('E4'), Q),
              (note('D4'), Q), (note('E4'), Q), (note('F#4'), H)]
    return t + seq(phrase, vel=80, gate=0.97)


def mk_whistling() -> bytes:
    """Whistling — simple pentatonic melody, 100 BPM, 2 bars."""
    t = tempo_event(100) + time_sig_event(4, 2)
    penta = [(note('C5'), Q), (note('D5'), Q), (note('E5'), H),
             (note('G5'), Q), (note('E5'), Q), (note('D5'), H),
             (note('C5'), H)]
    return t + seq(penta, vel=75, gate=0.88)


def mk_wind_surf() -> bytes:
    """Wind / surf — sustained noise swell, 100 BPM, 2 bars."""
    t = tempo_event(100) + time_sig_event(4, 2)
    return (t
            + note_on(0, note('C3'), 70)
            + note_off(W * 2, note('C3')))


def mk_wood_blocks() -> bytes:
    """Wood blocks — alternating C4 (hi) / G4 (lo) at 120 BPM sixteenths."""
    t = tempo_event(120) + time_sig_event(4, 2)
    hi, lo = note('C4'), note('G4')
    notes = b''
    for i in range(32):   # 2 bars of 16ths
        n = hi if i % 2 == 0 else lo
        v = 95 if i % 4 == 0 else 75
        on = max(1, int(S * 0.35))
        notes += note_on(0, n, v) + note_off(on, n) + var_len(S - on)
    return t + notes


# ---------------------------------------------------------------------------
# Patch → MIDI mapping
# ---------------------------------------------------------------------------

PATCHES = [
    ("acid_reverb",         mk_acid_reverb),
    ("banjo",               mk_banjo),
    ("bass_drum",           mk_bass_drum),
    ("bell",                mk_bell),
    ("bongo_drums",         mk_bongo_drums),
    ("bowed_bass",          mk_bowed_bass),
    ("brass",               mk_brass),
    ("clarinet",            mk_clarinet),
    ("cow_bell",            mk_cow_bell),
    ("cymbal",              mk_cymbal),
    ("delay_lead",          mk_delay_lead),
    ("dog_whistle",         mk_dog_whistle),
    ("english_horn",        mk_english_horn),
    ("flute",               mk_flute),
    ("glockenspiel",        mk_glockenspiel),
    ("gong",                mk_gong),
    ("gong_full",           mk_gong_full),
    ("group_strings",       mk_group_strings),
    ("harpsichord",         mk_harpsichord),
    ("juno_pad",            mk_juno_pad),
    ("juno_strings",        mk_juno_strings),
    ("organ_drawbar",       mk_organ_drawbar),
    ("percussion_noise",    mk_percussion_noise),
    ("pizzicato_violin",    mk_pizzicato_violin),
    ("rain",                mk_rain),
    ("reverb_pad",          mk_reverb_pad),
    ("sh_bass",             mk_sh_bass),
    ("snare_drum",          mk_snare_drum),
    ("strings_chorus_reverb", mk_strings_chorus_reverb),
    ("tb_bass",             mk_tb_bass),
    ("thunder",             mk_thunder),
    ("tom_tom",             mk_tom_tom),
    ("violin_vibrato",      mk_violin_vibrato),
    ("whistling",           mk_whistling),
    ("wind_surf",           mk_wind_surf),
    ("wood_blocks",         mk_wood_blocks),
]

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(description="Generate patch test MIDI fixtures.")
    parser.add_argument("--out-dir", default="midi",
                        help="Output directory for .mid files (default: midi/)")
    args = parser.parse_args()

    out_dir = args.out_dir
    os.makedirs(out_dir, exist_ok=True)

    print(f"Generating {len(PATCHES)} MIDI files → {out_dir}/")
    for (name, fn) in PATCHES:
        path = os.path.join(out_dir, f"{name}.mid")
        events = fn()
        write_smf(path, PPQN, events)

    print(f"\nDone — {len(PATCHES)} files written.")
    print(f"Each file is Format-0 SMF, 480 PPQN.")
    print(f"Re-run this script to regenerate after changing musical content.")


if __name__ == "__main__":
    main()
