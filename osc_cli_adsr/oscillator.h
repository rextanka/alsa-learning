#ifndef OSCILLATOR_H
#define OSCILLATOR_H

#include <stdint.h>
#include <stdbool.h>
#include "envelope.h" // Include the envelope header for fill buffer

typedef enum {
    OSC_SINE,
    OSC_SQUARE,
    OSC_TRIANGLE,
    OSC_SAWTOOTH
} OscillatorType;

typedef struct {
    OscillatorType type;
    
    // Rotor state (Primary for Sine)
    double x;
    double y;
    double cos_step;    
    double sin_step;    
    
    // Frequency state (Using double for sub-Hertz sweep precision)
    int sample_rate;
    double current_freq;
    
    // Transition state (Sweep/Glide)
    double target_freq;
    double freq_step;    
    bool transitioning;

    // Phase Accumulator (0.0 to 1.0)
    // Used for performance-optimized Square, Triangle, and Sawtooth
    double phase;

    int sample_count; // Counter for normalization intervals
} Oscillator;

// Initialize the oscillator
void osc_init(Oscillator *osc, OscillatorType type, double freq, int sample_rate);

// Set a new frequency target for sweeps or glides
void osc_set_target(Oscillator *osc, double target_freq, double duration_seconds);

// Fills a stereo buffer of S16_LE samples
void osc_fill_buffer(Oscillator *osc, adsr_t *env, int16_t *buffer, int frames);

#endif