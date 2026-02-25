# Bach MIDI Assets

This directory contains references for MIDI files used for engine validation and polyphonic stress testing.

## Pieces Used

### 1. BWV 578 - Fugue in G minor ("Little Fugue")
* **Why:** Excellent for testing independent melodic lines (counterpoint) and "Running Status" efficiency.
* **Source:** [Public Domain MIDI - Mutopia Project / MuseScore](https://musescore.com/user/204621/scores/195416)
* **Engine Focus:** Polyphonic clarity, British Church Organ timbre.

### 2. BWV 846 - Prelude in C major (WTC Book 1)
* **Why:** Consistent rhythmic pulse and overlapping note durations. Excellent for testing ADSR release timing.
* **Source:** [Public Domain MIDI](https://www.gutenberg.org/files/22239/22239-mid.zip)
* **Engine Focus:** Timing accuracy, legato transitions.

## Usage in Tests
The `test_bach_midi.cpp` unit test manually encodes the subjects of these pieces in raw hex to verify the `MidiParser` state machine without requiring external file dependencies during the flight.
