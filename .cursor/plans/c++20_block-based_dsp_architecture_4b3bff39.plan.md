---
name: C++20 Block-Based DSP Architecture
overview: Design a C++20 block-based DSP engine migrating from sample-by-sample C to vector processing, with performance profiling, graph-based node chaining, polyphony, and C interop.
todos: []
isProject: false
---

# C++20 Block-Based DSP Architecture Plan

## Current State Analysis

The existing C implementation (`osc_cli_adsr/`) uses:

- **Sample-by-sample processing**: `osc_fill_buffer()` calls `osc_next_value()` and `adsr_process()` per sample in a loop
- **Fixed buffer size**: 1024 frames (`BUFFER_FRAMES`)
- **Format**: `int16_t` (S16_LE) with stereo interleaving
- **Monophonic**: Single oscillator + envelope pair
- **Direct coupling**: Oscillator directly calls envelope's `adsr_process()` per sample
- **No performance tracking**: No timing measurements

Key algorithms to preserve:

- Rotor-based sine generation with periodic normalization
- Phase accumulator for square/triangle/sawtooth
- PolyBLEP anti-aliasing
- ADSR state machine with exponential decay/release
- Legato note-on support

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                    C Interface Layer                     │
│              (bridge/CInterface.h)                       │
└──────────────────────┬──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│              VoiceManager (Polyphony)                    │
│         ┌──────────────┐  ┌──────────────┐              │
│         │    Voice 1   │  │    Voice N   │              │
│         │  ┌─────────┐  │  │  ┌─────────┐ │              │
│         │  │ Graph   │  │  │  │ Graph   │ │              │
│         │  │Osc→Env→ │  │  │  │Osc→Env→ │ │              │
│         │  │ Filter  │  │  │  │ Filter  │ │              │
│         │  └─────────┘  │  │  └─────────┘ │              │
│         └──────────────┘  └──────────────┘              │
└──────────────────────┬──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│              Processor Base Class                        │
│         (audio/Processor.hpp)                           │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐             │
│  │Oscillator│  │ Envelope │  │  Filter  │             │
│  │Processor │  │Processor │  │Processor │             │
│  └──────────┘  └──────────┘  └──────────┘             │
└──────────────────────┬──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│              AudioDriver (HAL)                           │
│         (hal/include/AudioDriver.hpp)                   │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐             │
│  │  ALSA    │  │CoreAudio │  │  WASAPI  │             │
│  │ (Linux)  │  │ (macOS)  │  │(Windows)│             │
│  └──────────┘  └──────────┘  └──────────┘             │
└─────────────────────────────────────────────────────────┘
```

## 1. Node Interface: Processor Base Class

### File: `cxx/audio/Processor.hpp`

**Design:**

- Pure virtual base class using `std::span<float>` for block processing
- Performance profiling built into base class
- Thread-safe parameter updates using `std::atomic` where needed

**Key Methods:**

```cpp
// Forward declaration for Voice context
class Voice;

// Voice context interface for parameter querying (query pattern)
class VoiceContext {
public:
    virtual ~VoiceContext() = default;
    virtual uint8_t get_velocity() const = 0;
    virtual uint8_t get_aftertouch() const = 0;
    virtual float get_current_note() const = 0;
};

// Input source interface for pull model
class InputSource {
public:
    virtual ~InputSource() = default;
    // Pull data from this input source into output span
    virtual void pull(std::span<float> output, const VoiceContext* voice_context = nullptr) = 0;
};

class Processor : public InputSource {
public:
    virtual ~Processor() = default;
    
    // Pull Model: Pull data into output span (output "pulls" from this processor)
    // This processor will pull from its inputs if needed
    // voice_context: Optional Voice context for querying velocity/aftertouch (query pattern)
    void pull(std::span<float> output, const VoiceContext* voice_context = nullptr) override = 0;
    
    // Connect input sources (for pull model, processors pull from their inputs)
    void add_input(InputSource* input);
    void remove_input(InputSource* input);
    
    // Reset internal state (for voice stealing)
    virtual void reset() = 0;
    
    // Performance metrics (only active when AUDIO_ENABLE_PROFILING is set; otherwise zero cost)
    struct PerformanceMetrics {
        std::chrono::nanoseconds last_execution_time;
        std::chrono::nanoseconds max_execution_time;
        size_t total_blocks_processed;
    };
    
    PerformanceMetrics get_metrics() const;
    
protected:
    std::vector<InputSource*> inputs_;  // Input sources this processor pulls from
    
#if AUDIO_ENABLE_PROFILING
    void start_timer();
    void stop_timer();
#endif
};
```

**Rationale (Pull Model):**

- **Pull Model**: Output "pulls" data from processors, processors pull from their inputs. This is critical for:
  - **Feedback loops**: Nodes can pull from themselves (via delay buffers) naturally
  - **Lazy evaluation**: Data is computed only when requested
  - **Flexible graph topology**: Easier to handle cycles and complex routing
- `std::span<float>` provides bounds-safe buffer access without ownership; block length comes from span size (no hardcoded 1024).
- **InputSource interface**: Processors implement `InputSource` so they can be pulled from. Processors maintain a list of input sources they pull from.
- **Voice context parameter**: Optional `VoiceContext`* allows processors to query velocity/aftertouch when needed (query pattern). Graph passes Voice's context when calling `pull()`. Processors can ignore it if they don't need voice parameters.
- Performance tracking is compile-time optional; when disabled, no timer calls or branches in the hot path.
- Virtual destructor ensures proper cleanup in polymorphic hierarchies.

## 2. Performance Profiling System

### File: `cxx/audio/PerformanceProfiler.hpp`

**Design goals:**

- **Lightweight and minimally invasive**: Profiling must not materially affect real-time performance when enabled. Avoid heavy abstractions and per-sample work.
- **Embedding-friendly**: Design for possible use on embedded systems (e.g. no mandatory use of heavyweight timers or large aggregates). Keep code and data footprint small.
- **Optional**: Controlled by a compile-time flag (e.g. `AUDIO_ENABLE_PROFILING`). When disabled, profiling code compiles to zero cost (no timers, no branches in hot path).

**Implementation strategy:**

- **When `AUDIO_ENABLE_PROFILING` is off**: No timer calls, no extra fields in `Processor`; `get_metrics()` can return zeros or a fixed “disabled” value.
- **When on**: Use the lightest timing mechanism available. On **host**: `std::chrono::high_resolution_clock` or equivalent. On **embedded**: prefer a **cycle counter** (e.g. CPU cycle count or platform-specific tick) as the most lightweight, least invasive option—no syscalls, minimal latency. One start/stop per block per node, no per-sample work.
- Store only what’s needed: e.g. last execution time (nanoseconds) and optional max. Avoid min/max/avg histograms in the core path unless needed; optional separate “stats” layer can aggregate for host tools.
- No dynamic allocation in the profiling path.

**Interface (conceptual):**

```cpp
// When AUDIO_ENABLE_PROFILING is 1:
class PerformanceProfiler {
    // Host: std::chrono::nanoseconds; embedded: uint64_t cycle count
    std::chrono::nanoseconds execution_time_;  // or uint64_t cycles_
    
public:
    void start();   // single write (cycle count or chrono::now)
    void stop();    // single read, store difference
    std::chrono::nanoseconds elapsed() const;  // on embedded: cycles-to-ns if needed for API
    bool exceeds_budget(std::chrono::nanoseconds buffer_budget) const;
};
// Embedded: use cycle counter (e.g. ARM DWT CYCCNT, or RDTSC on x86) for minimal overhead.
```

**Integration:**

- Each `Processor::process()` call is optionally wrapped with profiler (macro or `if constexpr` so disabled build has no call overhead).
- VoiceManager aggregates only when profiling is enabled.
- C API exposes metrics only when profiling is compiled in; otherwise returns zeros or “disabled”.

## 3. Graph Logic: Node Chaining

### File: `cxx/audio/Graph.hpp`

**Design:**

- Directed graph of Processor nodes (not restricted to DAG): **feedback loops must be supported** for effects such as chorus, delay, and reverb.
- Buffer ownership: Graph owns intermediate buffers, nodes operate on spans.
- Execution order: Use a valid schedule that respects dependencies; feedback edges are handled by nodes that introduce delay (e.g. delay lines, block-based effects) so the graph can be scheduled in fixed-size blocks.
- Support for parallel branches (e.g., multiple oscillators → mixer).

**Feedback handling:**

- Feedback is safe when the loop contains at least one “delay” (one-block or multi-block latency). Delay/Reverb/Chorus nodes provide this by reading from previous block(s) or internal state.
- Graph does not require cycle detection for scheduling; it relies on block-based processing and delay-bearing nodes in feedback paths.
- Optionally: graph validation can warn if a feedback loop has no delay-bearing node.

**Class Structure:**

```cpp
class Graph {
    struct Node {
        std::unique_ptr<Processor> processor;
        std::vector<size_t> input_indices;  // Indices into buffer pool
        std::vector<size_t> output_indices;
    };
    
    std::vector<Node> nodes_;
    std::vector<std::vector<float>> buffer_pool_;  // Intermediate buffers
    std::vector<size_t> execution_order_;  // Valid schedule (DAG where possible; feedback via delay nodes)
    
public:
    void add_node(std::unique_ptr<Processor> proc, 
                  const std::vector<size_t>& inputs,
                  const std::vector<size_t>& outputs);
    
    // Process with optional Voice context for parameter querying (query pattern)
    void process(std::span<float> final_output, const VoiceContext* voice_context = nullptr);
    
    void reset_all();
};
```

**Buffer Management:**

- Graph pre-allocates buffers based on **current block size** (not hardcoded).
- Nodes receive `std::span` views into buffer pool.
- No copying between nodes (zero-copy chaining).
- Final output span can be same buffer as last node's output (in-place).

**Voice Context Integration (Query Pattern):**

- When `Voice::process()` is called, it calls `Graph::process(output, this)` passing itself as `VoiceContext`*.
- Graph passes the `VoiceContext`* to each processor's `process()` method.
- Processors query `voice_context->get_velocity()` and `voice_context->get_aftertouch()` as needed.
- Processors that don't need voice parameters can ignore the `voice_context` parameter (nullptr-safe).

**Example Chain (with optional effect loop):**

```
Oscillator → Envelope → Filter → [Dry] → Output
                    ↑              ↓
                    └── Delay ←────┘   (feedback; delay provides block latency)
```

## 4. Polyphony: Voice and VoiceManager

### File: `cxx/audio/Voice.hpp`

**Design:**

- Encapsulates a complete signal chain (Graph) for one MIDI note
- **State management**: Each voice has `bool isActive` and `float currentNote` for simple, queryable state (e.g. for UI, voice stealing, and C API). `currentNote` holds the note number in use (e.g. MIDI 0–127 as float, or frequency); when idle it can be -1 or a sentinel
- **Per-voice parameters**: Tracks `velocity` (note-on velocity, 0–127) and `aftertouch` (polyphonic aftertouch, 0–127). These are **core requirements** for MIDI keyboard support:
  - **Velocity**: Set at note-on, can be updated during note (e.g. via MIDI velocity change messages)
  - **Aftertouch**: Continuous per-voice control while note is held (polyphonic aftertouch). Typically maps to filter cutoff, amplitude, or other timbral parameters
- Parameters are routed to processors in the Graph (e.g. velocity → envelope amplitude, aftertouch → filter cutoff)
- Handles note-on/note-off events

**Class Structure:**

```cpp
enum class VoiceState {
    Idle,
    Active,
    Releasing,
    Stealing  // Being stolen by new note
};

// Voice implements VoiceContext for query pattern
class Voice : public VoiceContext {
    bool isActive_ = false;
    float currentNote_ = -1.0f;   // -1 or sentinel when idle
    VoiceState state_;
    uint8_t velocity_;              // Note-on velocity (0-127)
    uint8_t aftertouch_ = 0;       // Polyphonic aftertouch (0-127)
    std::unique_ptr<Graph> graph_;
    
public:
    Voice(std::unique_ptr<Graph> graph);
    
    void note_on(uint8_t note, uint8_t velocity);
    void note_off();
    
    // Per-voice parameter control (required for MIDI velocity and aftertouch)
    void set_velocity(uint8_t velocity);        // Update velocity (affects envelope, amplitude)
    void set_aftertouch(uint8_t aftertouch);    // Polyphonic aftertouch (0-127)
    
    // VoiceContext interface (query pattern)
    uint8_t get_velocity() const override { return velocity_; }
    uint8_t get_aftertouch() const override { return aftertouch_; }
    float get_current_note() const override { return currentNote_; }
    
    bool is_active() const { return isActive_; }
    float current_note() const { return currentNote_; }
    bool is_idle() const;
    
    // Pull Model: Pull data from Graph into output
    void pull(std::span<float> output);
    void reset();
};
```

### File: `cxx/audio/VoiceManager.hpp`

**Design:**

- Manages pool of Voice instances
- **Render loop**: Each process call iterates over **active voices only** (using each voice’s `isActive`), renders each into a temporary buffer, then **sums into the final `std::span`** using a **SIMD-friendly mixer** (e.g. contiguous float buffers, loop in blocks of 4/8 floats for SSE/AVX, or use platform intrinsics / optional library). Final output is cleared once then accumulated (no unnecessary copies).
- Handles voice stealing when polyphony limit is reached (see below)

**Class Structure:**

```cpp
class VoiceManager {
    std::array<std::unique_ptr<Voice>, AUDIO_MAX_VOICES> voices_;  // compile-time size
    size_t max_polyphony_;  // runtime cap <= AUDIO_MAX_VOICES
    std::vector<float> mix_buffer_;  // Per-voice render target (sized to block size); SIMD-friendly alignment
    
public:
    VoiceManager(size_t max_voices = AUDIO_MAX_VOICES);  // default from build flag
    
    void note_on(uint8_t note, uint8_t velocity);
    void note_off(uint8_t note);
    void all_notes_off();
    
    // Per-voice parameter control (required for MIDI aftertouch)
    void set_aftertouch(uint8_t note, uint8_t aftertouch);  // Polyphonic aftertouch per note
    void set_channel_aftertouch(uint8_t aftertouch);        // Channel aftertouch (all active voices)
    
    // Render loop: for each active voice, pull into mix_buffer_, then sum into output (SIMD-friendly, Pull Model)
    void pull(std::span<float> output);
    
    Voice* allocate_voice(uint8_t note);
    void release_voice(uint8_t note);
    Voice* find_voice(uint8_t note);  // Find voice playing a specific note (for aftertouch routing)
};
```

**Render loop (Pull Model, conceptual):**

- Clear `output` (e.g. zero).
- For each voice with `voice->is_active()`:  
  - `voice->pull(mix_buffer_)` (pull model: voice pulls from its graph).  
  - Add `mix_buffer_` into `output` in a SIMD-friendly way (aligned float blocks).
- Optional: apply master gain or soft clip to `output`.

**Voice stealing algorithm (all voices busy, new note arrives):**

When all `AUDIO_MAX_VOICES` are in use (all still in envelope phase—attack/decay/sustain—or releasing) and a new note-on arrives:

1. **Prefer stealing a releasing voice**
  Among voices in `Releasing` (or envelope idle), choose the one that has been releasing the longest (or is closest to idle). Reuse it for the new note (reset, then note_on). This minimizes audible cut-off.
2. **If no releasing voice: steal an active voice**
  All 16 are still in attack/decay/sustain. Choose a victim by a deterministic policy, e.g.:  
  - **Lowest note** (lowest `currentNote`): frees low notes first, often less noticeable.  
  - Or **oldest note-on**: first-come first-served for keeping, newest note steals oldest.  
  - Or **lowest priority** (e.g. lowest velocity).
   Document the chosen policy (e.g. “steal lowest note” or “steal oldest”). Reset the stolen voice, then assign the new note (note_on).
3. **After stealing**
  Update victim’s `isActive` and `currentNote`; call `voice->reset()` then `voice->note_on(new_note, new_velocity)` so the new note plays cleanly.

This ensures that when 16 notes are still in the envelope phase and new notes come in, there is a well-defined, predictable stealing behavior.

**Polyphony limit:**

- **Compile-time configurable**: e.g. `AUDIO_MAX_VOICES` (default 16). VoiceManager and any static voice pools use this constant so the footprint and behavior are fixed at build time.
- Runtime “max voices” can still cap below `AUDIO_MAX_VOICES` if desired, but the upper bound is set at compile time.

## 5. Mock Audio Driver (Testing)

### File: `cxx/hal/null/NullAudioDriver.hpp` or `cxx/hal/file/FileAudioDriver.hpp`

**Design:**

- **Mock driver for testing**: Allows DSP testing without hardware configuration
- Works on both Razer (Linux) and Mac immediately
- Two options:
  - **NullAudioDriver**: Discards output (fastest, for performance testing)
  - **FileAudioDriver**: Writes to WAV file (for audio verification)

**NullAudioDriver:**

```cpp
namespace hal {

class NullAudioDriver : public AudioDriver {
public:
    int open(unsigned int sample_rate, 
             unsigned int channels,
             unsigned int block_size) override {
        sample_rate_ = sample_rate;
        channels_ = channels;
        block_size_ = block_size;
        return 0;  // Always succeeds
    }
    
    void close() override {}
    
    int write(std::span<const float> buffer, size_t frames) override {
        // Discard output - just count frames for testing
        total_frames_written_ += frames;
        return frames;
    }
    
    unsigned int get_sample_rate() const override { return sample_rate_; }
    unsigned int get_block_size() const override { return block_size_; }
    bool is_open() const override { return true; }
    
private:
    unsigned int sample_rate_;
    unsigned int channels_;
    unsigned int block_size_;
    size_t total_frames_written_ = 0;
};

} // namespace hal
```

**FileAudioDriver:**

- Writes interleaved float samples to WAV file
- Uses simple WAV header format
- Allows audio verification without hardware
- Useful for automated testing and debugging

**Usage:**

- Test oscillators: Create oscillator → pull blocks → write to mock driver → verify output
- Test performance: Measure execution time without audio hardware latency
- Test on any platform: Works identically on Linux, macOS, Windows

## 6. Cross-Platform Audio Driver (HAL)

### File: `cxx/hal/include/AudioDriver.hpp` (Base Class)

**Design:**

- Abstract base class for platform-specific audio drivers
- **Critical requirement**: Must support **ALSA (Linux)**, **CoreAudio (macOS)**, and **WASAPI (Windows)**
- Unified interface for audio I/O across platforms
- Handles format conversion: internal `float` (-1.0 to 1.0) to platform format (typically `int16_t` or `float32`)

**Key Methods:**

```cpp
namespace hal {

class AudioDriver {
public:
    virtual ~AudioDriver() = default;
    
    // Open audio device with specified parameters
    virtual int open(unsigned int sample_rate, 
                     unsigned int channels,
                     unsigned int block_size) = 0;
    
    // Close audio device
    virtual void close() = 0;
    
    // Write audio data (pull model: driver pulls from VoiceManager output)
    // Format: interleaved float samples (-1.0 to 1.0)
    virtual int write(std::span<const float> buffer, size_t frames) = 0;
    
    // Get current sample rate (may differ from requested)
    virtual unsigned int get_sample_rate() const = 0;
    
    // Get current block size
    virtual unsigned int get_block_size() const = 0;
    
    // Check if device is open
    virtual bool is_open() const = 0;
};

} // namespace hal
```

### Platform Implementations

**File: `cxx/hal/alsa/AlsaAudioDriver.hpp` (Linux)**

- Implements `AudioDriver` using ALSA (`libasound`)
- Uses `snd_pcm_open()`, `snd_pcm_hw_params_set_*()`, `snd_pcm_writei()`
- Format: Converts `float` to `S16_LE` (signed 16-bit little-endian)
- Handles underruns via `snd_pcm_prepare()`
- Preserves existing ALSA code patterns from `osc_cli_adsr/alsa_output.c`

**File: `cxx/hal/coreaudio/CoreAudioDriver.hpp` (macOS)**

- Implements `AudioDriver` using CoreAudio (`AudioToolbox` framework)
- Uses `AudioComponentInstanceNew()`, `AudioUnitSetProperty()`, `AudioUnitRender()`
- Format: Uses `kAudioFormatLinearPCM` with `float32` (can avoid conversion)
- Handles buffer callbacks for real-time audio

**File: `cxx/hal/wasapi/WasapiAudioDriver.hpp` (Windows)**

- Implements `AudioDriver` using WASAPI (Windows Audio Session API)
- Uses `IMMDeviceEnumerator`, `IAudioClient`, `IAudioRenderClient`
- Format: Converts `float` to `int16_t` or uses `float32` if supported
- Handles buffer underruns and device changes

**Platform Detection:**

- Use compile-time platform detection (`#ifdef __linux__`, `#ifdef __APPLE__`, `#ifdef _WIN32`)
- Factory function or `std::unique_ptr<AudioDriver>` creation based on platform
- CMake handles platform-specific libraries (ALSA on Linux, CoreAudio on macOS, WASAPI on Windows)

**Integration:**

- VoiceManager's `pull()` output connects to AudioDriver's `write()`
- AudioDriver runs in separate thread or callback (platform-dependent)
- Pull model: AudioDriver requests data from VoiceManager when buffer is ready

## 7. C Interop Layer

### File: `cxx/bridge/CInterface.h` and `cxx/bridge/CInterface.cpp`

**Design:**

- Opaque handle types (void pointers) to hide C++ types
- C-linkage functions wrapping C++ classes
- Stable API for Swift/C# P/Invoke

**Key Functions:**

```c
// Opaque handles
typedef void* AudioEngineHandle;
typedef void* VoiceManagerHandle;

// Engine lifecycle
AudioEngineHandle audio_engine_create(unsigned int sample_rate, 
                                      unsigned int block_size);
void audio_engine_destroy(AudioEngineHandle engine);

// MIDI control
void audio_engine_note_on(AudioEngineHandle engine, 
                          uint8_t note, uint8_t velocity);
void audio_engine_note_off(AudioEngineHandle engine, uint8_t note);

// Per-voice parameter control (required for MIDI aftertouch)
void audio_engine_set_aftertouch(AudioEngineHandle engine, 
                                 uint8_t note, uint8_t aftertouch);  // Polyphonic aftertouch
void audio_engine_set_channel_aftertouch(AudioEngineHandle engine, 
                                         uint8_t aftertouch);        // Channel aftertouch (all voices)

// Audio processing
void audio_engine_process(AudioEngineHandle engine, 
                          float* output, size_t frames);

// Performance metrics
struct PerformanceStats {
    uint64_t max_node_time_ns;
    uint64_t total_processing_time_ns;
    uint32_t active_voices;
};
void audio_engine_get_stats(AudioEngineHandle engine, 
                             struct PerformanceStats* stats);
```

**Implementation Notes:**

- Use `extern "C"` linkage
- Cast handles to/from C++ types internally
- Return error codes (int) for failure cases
- No C++ exceptions cross the C boundary (catch and convert)

## File Structure

```
cxx/
├── audio/
│   ├── Processor.hpp              # Base class with std::span<float> (Pull Model)
│   ├── PerformanceProfiler.hpp   # Timing infrastructure
│   ├── oscillator/
│   │   ├── OscillatorProcessor.hpp    # Base oscillator class
│   │   ├── SineOscillatorProcessor.hpp    # Sine (rotor-based)
│   │   ├── SquareOscillatorProcessor.hpp   # Square (phase + PolyBLEP)
│   │   ├── SawtoothOscillatorProcessor.hpp # Sawtooth (phase + PolyBLEP)
│   │   └── TriangleOscillatorProcessor.hpp # Triangle (phase-based)
│   ├── envelope/
│   │   ├── EnvelopeProcessor.hpp      # Base envelope class
│   │   ├── ADSREnvelopeProcessor.hpp  # ADSR (migrate from C)
│   │   └── ADEnvelopeProcessor.hpp    # AD (Attack-Decay only)
│   ├── Graph.hpp                  # Node chaining logic (Pull Model)
│   ├── Voice.hpp                  # Single voice encapsulation
│   └── VoiceManager.hpp           # Polyphony management
├── hal/
│   ├── include/
│   │   └── AudioDriver.hpp        # Abstract base class
│   ├── null/
│   │   └── NullAudioDriver.hpp    # Mock driver (discards output)
│   ├── file/
│   │   └── FileAudioDriver.hpp    # Mock driver (writes WAV file)
│   ├── alsa/
│   │   └── AlsaAudioDriver.hpp    # Linux ALSA implementation
│   ├── coreaudio/
│   │   └── CoreAudioDriver.hpp    # macOS CoreAudio implementation
│   └── wasapi/
│       └── WasapiAudioDriver.hpp  # Windows WASAPI implementation
├── bridge/
│   ├── CInterface.h               # C API declarations
│   └── CInterface.cpp             # C API implementations
└── main.cpp                       # Test/demo entry point
```

## Oscillator Class Hierarchy

### File: `cxx/audio/oscillator/OscillatorProcessor.hpp` (Base Class)

**Design:**

- Base class `OscillatorProcessor` inherits from `Processor`
- Common functionality: frequency management, frequency ramping/glide, sample rate
- Shared state: `current_freq`, `target_freq`, `freq_step`, `transitioning`, `sample_rate`
- Pure virtual method: `generate_sample()` for waveform-specific generation
- Block-based `process()` calls `generate_sample()` for each sample in the block

**Key Methods:**

```cpp
class OscillatorProcessor : public Processor {
protected:
    double current_freq_;
    double target_freq_;
    double freq_step_;
    bool transitioning_;
    int sample_rate_;
    
public:
    OscillatorProcessor(int sample_rate);
    
    void set_frequency(double freq);
    void set_frequency_glide(double target_freq, double duration_seconds);
    
    // Pull Model: Pull data into output span (oscillators are source nodes, generate directly)
    void pull(std::span<float> output, const VoiceContext* voice_context = nullptr) override;
    
    void reset() override;
    
protected:
    // Pure virtual: each subclass implements waveform generation
    virtual double generate_sample() = 0;
    
    // Helper: update frequency ramp if transitioning
    void update_frequency_ramp();
};
```

### Subclasses: Sine, Square, Sawtooth, Triangle

**File: `cxx/audio/oscillator/SineOscillatorProcessor.hpp`**

- Uses rotor-based generation (preserves existing C implementation)
- State: `x`, `y`, `cos_step`, `sin_step`, `sample_count`
- Periodic normalization every 1024 samples (preserves existing behavior)
- Implements `generate_sample()` returning `y` from rotor

**File: `cxx/audio/oscillator/SquareOscillatorProcessor.hpp`**

- Uses phase accumulator (0.0 to 1.0)
- State: `phase`
- Implements `generate_sample()` with PolyBLEP anti-aliasing (preserves existing C logic)
- Phase wraps at 1.0

**File: `cxx/audio/oscillator/SawtoothOscillatorProcessor.hpp`**

- Uses phase accumulator
- State: `phase`
- Implements `generate_sample()` with PolyBLEP anti-aliasing (preserves existing C logic)

**File: `cxx/audio/oscillator/TriangleOscillatorProcessor.hpp`**

- Uses phase accumulator
- State: `phase`
- Implements `generate_sample()` with naive triangle (preserves existing C logic)

**Migration Notes:**

- Preserve all existing algorithms: rotor normalization, PolyBLEP, phase accumulation
- Convert sample-by-sample loop to block-based: `pull()` fills entire `std::span<float>` output (pull model: oscillators are source nodes)
- Frequency ramping logic moves to base class (shared by all oscillators)
- Future: `WavetableOscillatorProcessor` can inherit from `OscillatorProcessor` and override `generate_sample()`

## Envelope Class Hierarchy

### File: `cxx/audio/envelope/EnvelopeProcessor.hpp` (Base Class)

**Design Options:**

**Option A: Base class with configurable stages (Recommended)**

- Base `EnvelopeProcessor` with a flexible state machine
- Subclasses configure which stages are enabled (ADSR, AD, AR, etc.)
- More flexible, less code duplication

**Option B: Separate subclasses for each envelope type**

- `ADSREnvelopeProcessor`, `ADEnvelopeProcessor`, `AREnvelopeProcessor`, etc.
- More explicit, but potential code duplication

**Chosen: Option A (Configurable Stages)**

```cpp
enum class EnvelopeStage {
    Attack,
    Decay,
    Sustain,
    Release
};

class EnvelopeProcessor : public Processor {
protected:
    enum class State {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release
    };
    
    State state_;
    double current_gain_;
    double sample_rate_;
    
    // Stage configuration (set by subclasses)
    bool has_attack_;
    bool has_decay_;
    bool has_sustain_;
    bool has_release_;
    
    // Stage parameters
    double attack_step_;      // Linear attack increment
    double decay_mult_;       // Exponential decay multiplier
    double sustain_level_;    // Sustain level (0.0-1.0)
    double release_mult_;     // Exponential release multiplier
    
public:
    EnvelopeProcessor(int sample_rate);
    
    // Configure stages (called by subclasses in constructor)
    void configure_stages(bool attack, bool decay, bool sustain, bool release);
    
    // Set stage parameters
    void set_attack_time(double seconds);
    void set_decay_time(double seconds);
    void set_sustain_level(double level);
    void set_release_time(double seconds);
    
    void note_on();
    void note_off();
    
    // Pull Model: Pull data into output span (pulls from input if connected)
    void pull(std::span<float> output, const VoiceContext* voice_context = nullptr) override;
    
    void reset() override;
    
protected:
    // State machine step (called per sample)
    double process_sample();
};
```

### Subclasses: ADSR, AD

**File: `cxx/audio/envelope/ADSREnvelopeProcessor.hpp`**

- Inherits from `EnvelopeProcessor`
- Constructor calls `configure_stages(true, true, true, true)`
- Preserves all existing ADSR logic: exponential decay/release, linear attack, legato support, parameter slewing

**File: `cxx/audio/envelope/ADEnvelopeProcessor.hpp`**

- Inherits from `EnvelopeProcessor`
- Constructor calls `configure_stages(true, true, false, false)`
- Attack-Decay only: attack to peak, decay to zero (no sustain, no release)
- Useful for percussive sounds

**Migration Notes:**

- Preserve existing ADSR state machine logic (exponential coefficients, legato, slewing)
- Convert `adsr_process()` sample-by-sample to block-based `pull()` that fills output span (pull model: envelope pulls from oscillator input)
- Base class handles state transitions; subclasses configure which stages are active
- Future envelope types (AR, ADR, etc.) can be added as subclasses

**Alternative Consideration:**

- Could use a template-based approach: `EnvelopeProcessor<Stages...>` where Stages is a compile-time list
- Trade-off: More compile-time flexibility vs. runtime configuration. Current approach (runtime config) is simpler and more flexible for dynamic voice creation.

## Migration Strategy

1. **Phase 1**: Implement `Processor` base class and `PerformanceProfiler`
2. **Phase 2**: Implement `OscillatorProcessor` base class and migrate oscillators:
  - Create base class with frequency management
  - Migrate Sine (rotor-based)
  - Migrate Square (phase + PolyBLEP)
  - Migrate Sawtooth (phase + PolyBLEP)
  - Migrate Triangle (phase-based)
3. **Phase 3**: Implement `EnvelopeProcessor` base class and migrate envelope:
  - Create base class with configurable stages
  - Migrate ADSR (preserve all existing logic)
  - Add AD envelope subclass
4. **Phase 4**: Implement `Graph` for chaining (Pull Model)
5. **Phase 5**: Implement `Voice` and `VoiceManager` (Pull Model)
6. **Phase 6**: Implement cross-platform `AudioDriver` HAL:
  - Base `AudioDriver` interface
  - ALSA implementation (Linux)
  - CoreAudio implementation (macOS)
  - WASAPI implementation (Windows)
7. **Phase 7**: Implement C interop layer
8. **Phase 8**: Integration testing and performance validation

## Key Design Decisions

- **Block size**: **Not hardcoded.** Configurable at engine creation (e.g. `block_size` parameter). Default 1024 frames is a reasonable starting value; lower values (e.g. 256, 512) can be used to reduce latency when needed.
- **Format**: Use `float` internally (-1.0 to 1.0), convert to `int16_t` at HAL boundary.
- **Buffer ownership**: Graph owns buffers (sized from current block size), nodes use spans (zero-copy).
- **Thread safety**: Parameter updates use `std::atomic`, processing is single-threaded initially.
- **Performance**: Lightweight, compile-time optional profiling; when enabled, minimal overhead and embedding-friendly.
- **Polyphony**: **Compile-time constant** (e.g. `AUDIO_MAX_VOICES=16`); default 16 voices, adjustable via build flag.
- **Pull Model**: **Critical architecture decision** - output pulls from graph, processors pull from inputs. Natural support for feedback loops (delay nodes pull from their delay buffers). No explicit execution order needed.
- **Graph**: Supports **feedback loops** for effects (chorus, delay, reverb); pull model makes feedback natural via delay buffers.
- **Cross-platform audio**: HAL layer supports **ALSA (Linux)**, **CoreAudio (macOS)**, and **WASAPI (Windows)**. Platform-specific implementations in separate directories (`hal/alsa/`, `hal/coreaudio/`, `hal/wasapi/`).
- **State**: Each voice exposes `bool isActive` and `float currentNote` for rendering, stealing, and C API.
- **Render loop**: VoiceManager iterates active voices only and uses a **SIMD-friendly mixer** to sum into the final `std::span` (aligned float blocks, optional SSE/AVX or cycle-counter path).
- **Voice stealing**: When all voices are busy (e.g. still in envelope phase), steal in order: (1) a releasing voice (longest releasing first), else (2) an active voice (e.g. lowest note or oldest note-on). Reset stolen voice then assign new note.
- **Per-voice parameter control**: **Core requirement** for MIDI support:
  - **Velocity**: Set at note-on (0–127), can update during note
  - **Aftertouch**: Polyphonic aftertouch (0–127) per voice while note is held. VoiceManager routes aftertouch to the correct voice by note number. Channel aftertouch applies to all active voices.
  - Parameters map to processors in Graph (e.g. velocity → envelope amplitude, aftertouch → filter cutoff or amplitude modulation)
  - **Query pattern**: Processors query voice parameters via Voice context passed during `pull()`; Voice does not push parameters to processors
- **Embedded profiling**: Prefer **cycle counter** (e.g. DWT CYCCNT / RDTSC) for minimal overhead.

## Parameter Routing Design

**Velocity:**

- Set at `note_on()` (0–127 MIDI velocity)
- Can be updated via `set_velocity()` during note (e.g. MIDI velocity change messages)
- Typically routes to: envelope amplitude, oscillator level, or filter resonance
- Implementation: Voice stores velocity; processors in Graph query it via a parameter interface (e.g. `get_velocity()` or parameter bus)

**Aftertouch:**

- **Polyphonic aftertouch**: Per-voice control (0–127) while note is held
- VoiceManager provides `set_aftertouch(note, value)` to route to the correct voice
- **Channel aftertouch**: Single value applied to all active voices (monophonic aftertouch fallback)
- Typically routes to: filter cutoff, amplitude modulation, vibrato depth, or timbral changes
- Implementation: Voice stores aftertouch; processors query it and apply modulation (e.g. filter cutoff = base + aftertouch * scale)

**Parameter Access Pattern (Query Pattern):**

- **Chosen approach**: **Query pattern** - processors query voice parameters when needed
- Voice exposes `get_velocity()` and `get_aftertouch()` for processors to query
- Processors in Graph receive a **Voice context** (or pointer/reference) during `pull()` that allows them to query current parameter values
- Processors query parameters each block (or as needed) rather than Voice pushing updates to processors
- Benefits: Flexible (processors decide when/how to use parameters), decoupled (Voice doesn't need to know processor internals), allows multiple processors to use the same parameter differently
- Implementation: Graph passes Voice reference/pointer to processors, or processors receive a `VoiceContext` interface with `get_velocity()` / `get_aftertouch()` methods

## Leveraging Existing C Code

**Oscillators:**

- **Preserve algorithms**: Rotor-based sine with normalization, phase accumulator for square/triangle/saw, PolyBLEP anti-aliasing
- **Class hierarchy**: Base `OscillatorProcessor` with shared frequency management; subclasses (`SineOscillatorProcessor`, `SquareOscillatorProcessor`, etc.) implement waveform-specific `generate_sample()`
- **Migration**: Convert sample-by-sample `osc_next_value()` loop to block-based `pull()` that fills `std::span<float>` (pull model: oscillators are source nodes, generate directly)
- **Future**: `WavetableOscillatorProcessor` can inherit from base class

**Envelopes:**

- **Preserve algorithms**: ADSR state machine with exponential decay/release, linear attack, legato support, parameter slewing
- **Class hierarchy**: Base `EnvelopeProcessor` with configurable stages (attack/decay/sustain/release); subclasses configure which stages are active
- **Migration**: Convert sample-by-sample `adsr_process()` to block-based `pull()` that fills `std::span<float>` (pull model: envelope pulls from oscillator input, applies gain)
- **Subclasses**: `ADSREnvelopeProcessor` (all stages), `ADEnvelopeProcessor` (attack-decay only)
- **Alternative considered**: Template-based approach rejected in favor of runtime configuration for flexibility

## Open Questions

1. **Embedded profiling timer**: **Cycle counter** is the preferred option for embedded (most lightweight, least invasive).

