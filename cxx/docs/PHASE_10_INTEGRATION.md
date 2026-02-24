# Phase 10 Integration Brief: Musical Clock & Voice Triggering

## 1. Overview
The objective is to establish a sample-accurate link between the `MusicalClock` and the `VoiceManager`. The clock provides the temporal "where," and the `VoiceManager` provides the sonic "what."

## 2. Core Architecture: The Passive Pull
The `MusicalClock` must remain a **Passive Data Provider**. To maintain thread safety and precision, the audio callback (the "Consumer") is responsible for querying the clock state.

* **MusicalClock**: Maintains the `absoluteTick` based on the number of samples processed.
* **Edge Trigger Logic**: The system must detect when a "Beat" boundary (960 PPQ) has been crossed between the start and the end of an audio block.



## 3. Implementation Logic (Pseudo-C++)
To prevent "double-triggering" or missing beats, the callback must track the `lastProcessedTick`.

```cpp
// Logic to be implemented in the audio callback or test loop
void processAudio(float* buffer, int frames) {
    // 1. Update Clock
    clock.advance(frames);
    uint64_t currentTick = clock.getAbsoluteTick();

    // 2. Detect Beat Crossing (960 ticks per quarter note)
    uint64_t currentBeat = currentTick / 960;
    uint64_t lastBeat = lastProcessedTick / 960;

    if (currentBeat > lastBeat) {
        // We have hit a new beat
        int beatInBar = (currentBeat % 4) + 1; // 1, 2, 3, 4 cadence

        if (beatInBar == 1) {
            voiceManager.noteOn("C5", 0.8f); // Accent beat
        } else {
            voiceManager.noteOn("C4", 0.5f); // Weak beats
        }
    }
    
    lastProcessedTick = currentTick;

    // 3. Render Audio
    voiceManager.pull(buffer, frames);
}
```

## 4. Critical Constraints
* **Build Visibility**: macOS `arm64` requires `-fvisibility=default` in `CMakeLists.txt` to ensure RTTI (Run-Time Type Information) is consistent across the library and the test executable.
* **No dynamic_cast**: Avoid using `dynamic_cast` within the real-time audio thread to prevent `EXC_BAD_ACCESS` crashes related to visibility issues. Use `static_cast` only after manual type verification via enums or unique IDs.
* **Initialization Sequence**: 
    1.  Instantiate `MusicalClock` and `VoiceManager`.
    2.  Configure `ADSR` parameters (Attack: 0ms, Decay: 100ms, Sustain: 0.0, Release: 100ms) for a percussive response.
    3.  Start `AudioDriver` as the final step.

## 5. Success Criteria
* **Timing**: Log timestamps in the terminal must show ~750ms intervals (consistent with 80 BPM).
* **Amplitude**: `Peak Amplitude` output must be > 0.0 during pulses (verifying the envelope-oscillator link).
* **Audibility**: A distinct "High-Low-Low-Low" percussive cadence (C5 followed by three C4 notes) must be audible for the duration of the 8 bars.

## 6. Project SOP Reminders
* **Branching**: Use the naming convention `yyyymmddhhmm` for all feature branches.
* **Merging**: Manually create the PR on GitHub and perform a squash merge once the metronome test is verified and pushed.