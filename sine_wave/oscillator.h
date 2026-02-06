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
    int frequency;
    int sample_count; // Counter for normalization intervals
} Oscillator;

// Initialize the oscillator
void osc_init(Oscillator *osc, OscillatorType type, int freq, int sample_rate);

// Fills a stereo buffer of S16_LE samples
void osc_fill_buffer(Oscillator *osc, int16_t *buffer, int frames);

#endif