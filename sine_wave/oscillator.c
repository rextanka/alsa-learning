#include "oscillator.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// How often to correct floating point drift
#define NORMALIZE_INTERVAL 1024

void osc_init(Oscillator *osc, OscillatorType type, int freq, int sample_rate) {
    osc->type = type;
    osc->frequency = freq;
    osc->sample_rate = sample_rate;
    
    // Initial Rotor State
    osc->x = 1.0;
    osc->y = 0.0;
    osc->sample_count = 0; // Initialize the counter

    // Pre-calculate rotation step
    double angle_per_sample = 2.0 * M_PI * freq / sample_rate;
    osc->cos_step = cos(angle_per_sample);
    osc->sin_step = sin(angle_per_sample);
}

static double osc_next_value(Oscillator *osc) {
    switch (osc->type) {
        case OSC_SINE: {
            // 1. Complex rotation (The Rotor "Nudge")
            double next_x = osc->x * osc->cos_step - osc->y * osc->sin_step;
            double next_y = osc->x * osc->sin_step + osc->y * osc->cos_step;
            osc->x = next_x;
            osc->y = next_y;

            // 2. Periodic Normalization
            // We check if it's time to force the rotor back to a length of 1.0
            if (++osc->sample_count >= NORMALIZE_INTERVAL) {
                double magnitude = sqrt(osc->x * osc->x + osc->y * osc->y);
                osc->x /= magnitude;
                osc->y /= magnitude;
                osc->sample_count = 0;
            }

            return osc->y;
        }
        default:
            return 0.0;
    }
}

void osc_fill_buffer(Oscillator *osc, int16_t *buffer, int frames) {
    const double amplitude = 32767.0;

    for (int i = 0; i < frames; i++) {
        double val = osc_next_value(osc);
        int16_t sample = (int16_t)(val * amplitude);

        // Stereo Interleaving
        buffer[i * 2]     = sample; // Left
        buffer[i * 2 + 1] = sample; // Right
    }
}