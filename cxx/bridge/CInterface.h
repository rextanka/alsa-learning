/**
 * @file CInterface.h
 * @brief C-compatible API layer for cross-platform interoperability.
 * 
 * This file follows the project rules defined in .cursorrules:
 * - Interoperability: Core C++ logic wrapped in a C-compatible API to support:
 *   - macOS: Swift (via Bridge/C-Interop)
 *   - Linux: C++ GUI libraries (Qt, GTK, etc.)
 *   - Windows: .NET (C# via P/Invoke or C++/CLI)
 * - Modern C++: Target C++20/23 for all new code.
 */

#ifndef C_INTERFACE_H
#define C_INTERFACE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Oscillator type enum
// Algorithm-based (rotor/PolyBLEP): OSC_SINE .. OSC_SAWTOOTH
// Wavetable-based (one class, shape selected at create): OSC_WAVETABLE_*
enum OscillatorType {
    OSC_SINE = 0,
    OSC_SQUARE = 1,
    OSC_TRIANGLE = 2,
    OSC_SAWTOOTH = 3,
    OSC_WAVETABLE      = 4,  // Same as OSC_WAVETABLE_SINE (backward compat)
    OSC_WAVETABLE_SINE = 4,
    OSC_WAVETABLE_SAW  = 5,
    OSC_WAVETABLE_SQUARE   = 6,
    OSC_WAVETABLE_TRIANGLE = 7
};

// Opaque handle type
typedef void* OscillatorHandle;

// Oscillator API
OscillatorHandle oscillator_create(int type, unsigned int sample_rate);
void oscillator_destroy(OscillatorHandle handle);
int oscillator_set_frequency(OscillatorHandle handle, double freq);
int oscillator_set_frequency_glide(OscillatorHandle handle, double target_freq, double duration_seconds);
int oscillator_process(OscillatorHandle handle, float* output, size_t frames);
int oscillator_reset(OscillatorHandle handle);
int oscillator_get_metrics(OscillatorHandle handle, 
                           uint64_t* last_time_ns,
                           uint64_t* max_time_ns,
                           uint64_t* total_blocks);

// Wave type enum for wavetable oscillators
enum WaveType {
    WAVE_SINE = 0,
    WAVE_SAW = 1,
    WAVE_SQUARE = 2,
    WAVE_TRIANGLE = 3
};

// Set wave type for wavetable oscillator (only works if handle is a WavetableOscillator)
int set_osc_wavetype(void* instance, int type);

// Envelope type enum
enum EnvelopeType {
    ENV_ADSR = 0
};

// Envelope handle type
typedef void* EnvelopeHandle;

// Envelope API
EnvelopeHandle envelope_create(int type, unsigned int sample_rate);
void envelope_destroy(EnvelopeHandle handle);
int envelope_gate_on(EnvelopeHandle handle);
int envelope_gate_off(EnvelopeHandle handle);
int envelope_set_adsr(EnvelopeHandle handle, float attack, float decay, float sustain, float release);
int envelope_process(EnvelopeHandle handle, float* output, size_t frames);
int envelope_is_active(EnvelopeHandle handle);

#ifdef __cplusplus
}
#endif

#endif // C_INTERFACE_H
