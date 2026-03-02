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

// Modulation Parameters
#define PARAM_PITCH 0
#define PARAM_CUTOFF 1
#define PARAM_AMPLITUDE 2
#define PARAM_RESONANCE 3

// Modulation Sources (Modular Matrix)
#define MOD_SRC_ENVELOPE  0
#define MOD_SRC_LFO       1
#define MOD_SRC_VELOCITY  2
#define MOD_SRC_AFTERTOUCH 3

// Modulation Targets (Modular Matrix)
#define MOD_TGT_PITCH     0
#define MOD_TGT_CUTOFF    1
#define MOD_TGT_RESONANCE 2
#define MOD_TGT_AMPLITUDE 3
#define MOD_TGT_PULSEWIDTH 4

// Special IDs
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
AUDIO_API int engine_start(EngineHandle handle);
AUDIO_API int engine_stop(EngineHandle handle);
AUDIO_API int engine_set_bpm(EngineHandle handle, double bpm);
AUDIO_API double engine_get_bpm(EngineHandle handle);
AUDIO_API int engine_set_meter(EngineHandle handle, int beats_per_bar);
AUDIO_API int engine_get_musical_time(EngineHandle handle, int* bar, int* beat, int* tick);
AUDIO_API int engine_note_on_name(EngineHandle handle, const char* note_name, float velocity);
AUDIO_API int engine_note_off_name(EngineHandle handle, const char* note_name);
AUDIO_API void engine_flush_logs(EngineHandle handle);
AUDIO_API int engine_set_filter_type(EngineHandle handle, int type);
AUDIO_API int engine_set_delay_enabled(EngineHandle handle, int enabled);

// Modulation Matrix Control
AUDIO_API int engine_set_modulation(EngineHandle handle, int source, int target, float intensity);
AUDIO_API int engine_clear_modulations(EngineHandle handle);

// Patch Management
AUDIO_API int engine_save_patch(EngineHandle handle, const char* path);
AUDIO_API int engine_load_patch(EngineHandle handle, const char* path);

// Host & Device API
AUDIO_API int host_get_device_count();
AUDIO_API int host_get_device_name(int index, char* buffer, size_t buffer_size);
AUDIO_API int host_get_device_sample_rate(int index);

// Generic Parameter API
AUDIO_API int set_param(void* handle, const char* name, float value);

// Modular Routing API
AUDIO_API int engine_create_processor(EngineHandle handle, int type);
AUDIO_API int engine_connect_mod(EngineHandle handle, int source_id, int target_id, int param, float intensity);
AUDIO_API int engine_get_modulation_report(EngineHandle handle, char* buffer, size_t buffer_size);

// Logging API
AUDIO_API void audio_log_message(const char* tag, const char* message);
AUDIO_API void audio_log_event(const char* tag, float value);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_C_INTERFACE_H
