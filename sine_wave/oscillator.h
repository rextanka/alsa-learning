#ifndef OSCILLATOR_H
#define OSCILLATOR_H

#include <stdint.h>

typedef enum {
    OSC_SINE,
    OSC_SQUARE,   // Future use
    OSC_TRIANGLE  // Future use
} OscillatorType;

typedef struct {
    OscillatorType type;
    double x;
    double y;
    double cos_step;    
    double sin_step;    
    int sample_rate;
    
    // Interpolation state
    double current_freq;
    double target_freq;
    double freq_step;    // Frequency change per sample
    bool transitioning;

    int sample_count; 
} Oscillator;

// Updated Init
void osc_init(Oscillator *osc, OscillatorType type, double freq, int sample_rate);

// Set a new target to move toward
void osc_set_target(Oscillator *osc, double target_freq, double duration_seconds);

// Fills a stereo buffer of S16_LE samples
void osc_fill_buffer(Oscillator *osc, int16_t *buffer, int frames);

#endif