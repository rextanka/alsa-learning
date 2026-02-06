#include "oscillator.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// How often to correct floating point drift
#define NORMALIZE_INTERVAL 1024

void osc_init(Oscillator *osc, OscillatorType type, double freq, int sample_rate) {    osc->type = type;
    osc->current_freq = freq;
    osc->sample_rate = sample_rate;
    
    // Initial Rotor State
    osc->x = 1.0;
    osc->y = 0.0;
    osc->sample_count = 0; // Initialize the counter
    osc->transitioning = false;

    // Pre-calculate rotation step
    double angle_per_sample = 2.0 * M_PI * freq / sample_rate;
    osc->cos_step = cos(angle_per_sample);
    osc->sin_step = sin(angle_per_sample);
}

void osc_set_target(Oscillator *osc, double target_freq, double duration_seconds) {
    osc->target_freq = target_freq;
    if (duration_seconds > 0) {
        long total_samples = (long)(duration_seconds * osc->sample_rate);
        osc->freq_step = (target_freq - osc->current_freq) / total_samples;
        osc->transitioning = true;
    } else {
        osc->current_freq = target_freq;
        double angle = 2.0 * M_PI * target_freq / osc->sample_rate;
        osc->cos_step = cos(angle);
        osc->sin_step = sin(angle);
        osc->transitioning = false;
    }
}

static double osc_next_value(Oscillator *osc) {
    // 1. Handle Frequency Ramping (Sweep/Glide)
    // This happens regardless of waveform type so all oscillators stay in sync
    if (osc->transitioning) {
        osc->current_freq += osc->freq_step;
        
        // Check if we reached or exceeded the target frequency
        if ((osc->freq_step > 0 && osc->current_freq >= osc->target_freq) ||
            (osc->freq_step < 0 && osc->current_freq <= osc->target_freq)) {
            osc->current_freq = osc->target_freq;
            osc->transitioning = false;
        }

        // Update the rotor steps for the newly nudged frequency
        double angle = 2.0 * M_PI * osc->current_freq / osc->sample_rate;
        osc->cos_step = cos(angle);
        osc->sin_step = sin(angle);
    }

    // 2. Waveform Generation
    switch (osc->type) {
        case OSC_SINE: {
            // Complex rotation (The Rotor "Nudge")
            double next_x = osc->x * osc->cos_step - osc->y * osc->sin_step;
            double next_y = osc->x * osc->sin_step + osc->y * osc->cos_step;
            osc->x = next_x;
            osc->y = next_y;

            // Periodic Normalization to prevent floating-point drift
            if (++osc->sample_count >= 1024) {
                double magnitude = sqrt(osc->x * osc->x + osc->y * osc->y);
                osc->x /= magnitude;
                osc->y /= magnitude;
                osc->sample_count = 0;
            }

            return osc->y;
        }

        case OSC_SQUARE:
            // Future implementation: return (osc->y >= 0) ? 1.0 : -1.0;
            return 0.0;

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