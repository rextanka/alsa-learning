#ifndef AUDIO_C_INTERFACE_H
#define AUDIO_C_INTERFACE_H

#include <stdint.h>
#include <stddef.h>
 
/**
 * @file CInterface.h
 * @brief C-compatible API for the Audio Engine.
 */

#if defined(_WIN32)
    #ifdef AUDIO_ENGINE_EXPORTS
        #define AUDIO_API __declspec(dllexport)
    #else
        #define AUDIO_API __declspec(dllimport)
    #endif
#else
    #define AUDIO_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handles
typedef void* OscillatorHandle;
typedef void* EnvelopeHandle;
typedef void* EngineHandle;

// Oscillator Types
#define OSC_SINE 0
#define OSC_SQUARE 1
#define OSC_TRIANGLE 2
#define OSC_SAWTOOTH 3
#define OSC_WAVETABLE_SINE 4
#define OSC_WAVETABLE_SAW 5
#define OSC_WAVETABLE_SQUARE 6
#define OSC_WAVETABLE_TRIANGLE 7

// Wave Types for set_osc_wavetype
#define WAVE_SINE 0
#define WAVE_SAW 1
#define WAVE_SQUARE 2
#define WAVE_TRIANGLE 3

// Envelope Types
#define ENV_ADSR 0
#define ENV_AD 1

// Processor Types for registration
#define PROC_OSCILLATOR 0
#define PROC_LFO 1
#define PROC_FILTER 2
#define PROC_ENVELOPE 3
#define PROC_AUDIOTAP 4

// Modulation Parameters
#define PARAM_PITCH 0
#define PARAM_CUTOFF 1
#define PARAM_AMPLITUDE 2
#define PARAM_RESONANCE 3

// ---------------------------------------------------------------------------
// LFO API (Phase 15A — internal Voice LFO, musically correct path)
// ---------------------------------------------------------------------------

// LFO waveform constants (match LfoProcessor::Waveform enum order)
#define LFO_WAVEFORM_SINE     0
#define LFO_WAVEFORM_TRIANGLE 1
#define LFO_WAVEFORM_SQUARE   2
#define LFO_WAVEFORM_SAW      3

// LFO modulation depth targets (match ModulationTarget enum order)
// depth unit:
//   PITCH / CUTOFF : octaves (bipolar) — 0.05 = ±5% pitch, 1.0 = ±1 octave filter sweep
//   RESONANCE      : linear offset [0, 1]
//   AMPLITUDE      : linear gain factor [0, 1]
//   PULSEWIDTH     : linear offset [0, 1]
#define LFO_TARGET_PITCH      0
#define LFO_TARGET_CUTOFF     1
#define LFO_TARGET_RESONANCE  2
#define LFO_TARGET_AMPLITUDE  3
#define LFO_TARGET_PULSEWIDTH 4

// Deprecated Modular Matrix identifiers — retained for source compatibility only.
// The external integer-ID matrix (engine_connect_mod) is superseded by engine_set_lfo_*.
// Will be removed in Phase 16 when full port-based control routing is implemented.
#define MOD_SRC_ENVELOPE  0
#define MOD_SRC_LFO       1
#define MOD_SRC_VELOCITY  2
#define MOD_SRC_AFTERTOUCH 3
#define MOD_TGT_PITCH     0
#define MOD_TGT_CUTOFF    1
#define MOD_TGT_RESONANCE 2
#define MOD_TGT_AMPLITUDE 3
#define MOD_TGT_PULSEWIDTH 4
#define ALL_VOICES -1

/**
 * @brief Placeholder for future FFI initialization.
 */
AUDIO_API void audio_engine_init();

/**
 * @brief Placeholder for future FFI cleanup.
 */
AUDIO_API void audio_engine_cleanup();

// Oscillator API
AUDIO_API OscillatorHandle oscillator_create(int type, unsigned int sample_rate);
AUDIO_API void oscillator_destroy(OscillatorHandle handle);
AUDIO_API int oscillator_set_frequency(OscillatorHandle handle, double freq);
AUDIO_API int oscillator_set_frequency_glide(OscillatorHandle handle, double target_freq, double duration_seconds);
AUDIO_API int oscillator_process(OscillatorHandle handle, float* output, size_t frames);
AUDIO_API int oscillator_reset(OscillatorHandle handle);
AUDIO_API int oscillator_get_metrics(OscillatorHandle handle, uint64_t* last_time_ns, uint64_t* max_time_ns, uint64_t* total_blocks);
AUDIO_API int set_osc_wavetype(void* instance, int type);

// Envelope API
AUDIO_API EnvelopeHandle envelope_create(int type, unsigned int sample_rate);
AUDIO_API void envelope_destroy(EnvelopeHandle handle);
AUDIO_API int envelope_gate_on(EnvelopeHandle handle);
AUDIO_API int envelope_gate_off(EnvelopeHandle handle);
AUDIO_API int envelope_set_adsr(EnvelopeHandle handle, float attack, float decay, float sustain, float release);
AUDIO_API int envelope_set_ad(EnvelopeHandle handle, float attack, float decay);
AUDIO_API int envelope_process(EnvelopeHandle handle, float* output, size_t frames);
AUDIO_API int envelope_is_active(EnvelopeHandle handle);

// Engine API
AUDIO_API EngineHandle engine_create(unsigned int sample_rate);
AUDIO_API void engine_destroy(EngineHandle handle);
AUDIO_API void engine_note_on(EngineHandle handle, int note, float velocity);
AUDIO_API void engine_note_off(EngineHandle handle, int note);
AUDIO_API void engine_set_note_pan(EngineHandle handle, int note, float pan);
AUDIO_API int engine_set_adsr(EngineHandle handle, float attack, float decay, float sustain, float release);
AUDIO_API int engine_process(EngineHandle handle, float* output, size_t frames);
AUDIO_API void engine_process_midi_bytes(EngineHandle handle, const uint8_t* data, size_t size, uint32_t sampleOffset);
AUDIO_API int engine_start(EngineHandle handle);
AUDIO_API int engine_stop(EngineHandle handle);
AUDIO_API int engine_set_bpm(EngineHandle handle, double bpm);
AUDIO_API double engine_get_bpm(EngineHandle handle);
AUDIO_API int engine_set_meter(EngineHandle handle, int beats_per_bar);
AUDIO_API int engine_get_musical_time(EngineHandle handle, int* bar, int* beat, int* tick);
AUDIO_API int engine_get_total_ticks(EngineHandle handle, int64_t* ticks);
AUDIO_API int engine_note_on_name(EngineHandle handle, const char* note_name, float velocity);
AUDIO_API int engine_note_off_name(EngineHandle handle, const char* note_name);
AUDIO_API void engine_print_graph(EngineHandle handle);
AUDIO_API void engine_flush_logs(EngineHandle handle);
AUDIO_API int engine_set_filter_type(EngineHandle handle, int type);
AUDIO_API int engine_set_delay_enabled(EngineHandle handle, int enabled);
AUDIO_API int engine_get_xrun_count(EngineHandle handle);

// ---------------------------------------------------------------------------
// LFO API (Phase 15A)
// Configure the per-voice LFO and its modulation depth on the internal matrix.
// All four engine voices are updated atomically.
//
// engine_set_lfo_rate(handle, hz)           — set LFO rate in Hz [0.01, 20]
// engine_set_lfo_intensity(handle, 0..1)    — master intensity (scales all outputs)
// engine_set_lfo_waveform(handle, waveform) — LFO_WAVEFORM_* constant
// engine_set_lfo_depth(handle, target, d)   — depth for LFO_TARGET_* in octaves or
//                                             linear units (see constant comments)
// ---------------------------------------------------------------------------
AUDIO_API int engine_set_lfo_rate(EngineHandle handle, float hz);
AUDIO_API int engine_set_lfo_intensity(EngineHandle handle, float intensity);
AUDIO_API int engine_set_lfo_waveform(EngineHandle handle, int waveform);
AUDIO_API int engine_set_lfo_depth(EngineHandle handle, int target, float depth);

// Modulation Matrix Control
// engine_set_modulation removed in Phase 15 — use engine_set_lfo_* instead.
// engine_clear_modulations resets the internal Voice LFO matrix to defaults.
AUDIO_API int engine_clear_modulations(EngineHandle handle);

// Patch Management
// engine_save_patch removed — patch serialisation is not implemented (Phase 15 patches are
// authored by hand and loaded via engine_load_patch; there is no round-trip save path).
AUDIO_API int engine_load_patch(EngineHandle handle, const char* path);

// ---------------------------------------------------------------------------
// Phase 22A: SMF File Playback (channel-blind polyphonic)
// ---------------------------------------------------------------------------
// Load a Standard MIDI File (.mid) and play it back sample-accurately through
// the engine's VoiceManager.  All tracks are merged; MIDI channel is ignored
// at dispatch — all voices use the patch loaded via engine_load_patch /
// engine_add_module + engine_bake.  Phase 22B will add per-channel routing.
//
// Typical usage:
//   engine_load_patch(h, "patches/organ_drawbar.json"); // choose your timbre
//   engine_load_midi(h,  "midi/bwv578.mid");
//   engine_start(h);
//   engine_midi_play(h);
//   // … audio renders sample-accurately …
//   engine_midi_stop(h);
// ---------------------------------------------------------------------------

/** Load and parse a .mid file.  Returns 0 on success, -1 on parse error. */
AUDIO_API int engine_load_midi(EngineHandle handle, const char* path);

/** Start (or resume) MIDI file playback from the current playhead position. */
AUDIO_API int engine_midi_play(EngineHandle handle);

/** Pause MIDI file playback.  Playhead stays at its current position. */
AUDIO_API int engine_midi_stop(EngineHandle handle);

/** Rewind and stop.  Playhead returns to the start of the file. */
AUDIO_API int engine_midi_rewind(EngineHandle handle);

/** Query current playhead position in SMF ticks. */
AUDIO_API int engine_midi_get_position(EngineHandle handle, uint64_t* tick);

// Host & Device API
AUDIO_API int host_get_device_count();
AUDIO_API int host_get_device_name(int index, char* buffer, size_t buffer_size);
AUDIO_API int host_get_device_sample_rate(int index);

// Generic Parameter API
AUDIO_API int set_param(void* handle, const char* name, float value);

// Modular Routing API — DEPRECATED
// engine_create_processor / engine_connect_mod used an integer-ID external matrix
// with fundamental design issues (absolute-value SET semantics, ID-constant mismatch).
// Superseded by engine_set_lfo_* (Phase 15A) and will be removed in Phase 16
// when full port-based control routing replaces the matrix entirely.
[[deprecated("Use engine_add_module / engine_connect_ports / engine_bake instead. Removed in Phase 16.")]]
AUDIO_API int engine_create_processor(EngineHandle handle, int type);
[[deprecated("Use engine_set_lfo_depth / engine_set_lfo_rate instead. Removed in Phase 16.")]]
AUDIO_API int engine_connect_mod(EngineHandle handle, int source_id, int target_id, int param, float intensity);
[[deprecated("No replacement. Removed in Phase 16.")]]
AUDIO_API int engine_get_modulation_report(EngineHandle handle, char* buffer, size_t buffer_size);

// ---------------------------------------------------------------------------
// Phase 15: Module Registry Query + Chain Construction API
// ---------------------------------------------------------------------------

/**
 * @brief Number of registered module types in the ModuleRegistry.
 * Always valid after engine_create().
 */
AUDIO_API int engine_get_module_count(EngineHandle handle);

/**
 * @brief Copy the type name of the module at @p index into @p buffer.
 * Returns 0 on success, -1 if index is out of range or buffer too small.
 */
AUDIO_API int engine_get_module_type(EngineHandle handle, int index,
                                     char* buffer, size_t buf_size);

/**
 * @brief Begin building a new voice signal chain.
 * Append a module of the given registered type with the given tag.
 * Returns 0 on success, -1 if type_name is not registered.
 *
 * Must be called before engine_bake().
 * Calling engine_add_module() again after engine_bake() starts a new chain.
 */
AUDIO_API int engine_add_module(EngineHandle handle,
                                const char* type_name,
                                const char* tag);

/**
 * @brief Register a named port connection in the pending chain.
 * The connection is validated when engine_bake() is called.
 * Returns 0 always (validation deferred to bake).
 */
AUDIO_API int engine_connect_ports(EngineHandle handle,
                                   const char* from_tag,
                                   const char* from_port,
                                   const char* to_tag,
                                   const char* to_port);

/**
 * @brief Validate the pending chain and rebuild all voices.
 * Must be called before engine_start() and after all engine_add_module()
 * and engine_connect_ports() calls.
 * Returns 0 on success, -1 if validation fails.
 */
AUDIO_API int engine_bake(EngineHandle handle);

// AudioTap API
AUDIO_API int engine_audiotap_reset(EngineHandle handle);
AUDIO_API int engine_audiotap_read(EngineHandle handle, float* buffer, size_t frames);

// FX API
AUDIO_API int engine_set_chorus_mode(EngineHandle handle, int mode); // 0=Off, 1=I, 2=II, 3=I+II
AUDIO_API int engine_set_chorus_enabled(EngineHandle handle, int enabled);

// Logging API
AUDIO_API void audio_log_message(const char* tag, const char* message);
AUDIO_API void audio_log_event(const char* tag, float value);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_C_INTERFACE_H
