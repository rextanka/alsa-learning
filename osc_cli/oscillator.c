#include "oscillator.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// How often to correct floating point drift in the rotor
#define NORMALIZE_INTERVAL 1024

void osc_init(Oscillator *osc, OscillatorType type, double freq, int sample_rate) {
    osc->type = type;
    osc->current_freq = freq;
    osc->sample_rate = sample_rate;
    
    // Initial Rotor State
    osc->x = 1.0;
    osc->y = 0.0;
    osc->sample_count = 0;
    osc->phase = 0.0;
    osc->transitioning = false;

    // Pre-calculate initial rotation step
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
        // Instant jump if duration is 0
        osc->current_freq = target_freq;
        double angle = 2.0 * M_PI * target_freq / osc->sample_rate;
        osc->cos_step = cos(angle);
        osc->sin_step = sin(angle);
        osc->transitioning = false;
    }
}

// PolyBLEP smoothing function
// t: current phase (0 to 1), dt: phase increment (freq / sample_rate)

static double poly_blep(double t, double dt) {
    if (t < dt) {
        t /= dt;
        return t+t - t*t - 1.0;
    } else if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t*t + t+t + 1.0;
    }
    return 0.0;
}


static double osc_next_value(Oscillator *osc) {
    // 1. Handle Frequency Ramping (Sweep/Glide)
    if (osc->transitioning) {
        osc->current_freq += osc->freq_step;
        
        // Stop at target frequency to prevent overshoot
        if ((osc->freq_step > 0 && osc->current_freq >= osc->target_freq) ||
            (osc->freq_step < 0 && osc->current_freq <= osc->target_freq)) {
            osc->current_freq = osc->target_freq;
            osc->transitioning = false;
        }

        // Update rotor steps for the new frequency
        double angle = 2.0 * M_PI * osc->current_freq / osc->sample_rate;
        osc->cos_step = cos(angle);
        osc->sin_step = sin(angle);
    }

    // 2. Update Phase Accumulator (Cheap arithmetic for Square/Tri/Saw)
    osc->phase += osc->current_freq / osc->sample_rate;
    if (osc->phase >= 1.0) osc->phase -= 1.0;
    if (osc->phase < 0.0)  osc->phase += 1.0;

    // 3. Update Rotor (Used for high-quality SINE)
    double next_x = osc->x * osc->cos_step - osc->y * osc->sin_step;
    double next_y = osc->x * osc->sin_step + osc->y * osc->cos_step;
    osc->x = next_x;
    osc->y = next_y;

    // Periodic Normalization to prevent floating-point drift
    if (++osc->sample_count >= NORMALIZE_INTERVAL) {
        double magnitude = sqrt(osc->x * osc->x + osc->y * osc->y);
        osc->x /= magnitude;
        osc->y /= magnitude;
        osc->sample_count = 0;
    }

    // 4. Waveform Generation
    // Pre-calculate dt once for use in poly_blep
    double dt = osc->current_freq / osc->sample_rate;

    switch (osc->type) {
        case OSC_SINE:
            return osc->y;

        case OSC_SQUARE: {
            double naive = (osc->phase < 0.5) ? 0.5 : -0.5;
            // Apply correction at both transitions (0.0 and 0.5)
            naive += poly_blep(osc->phase, dt);
            naive -= poly_blep(fmod(osc->phase + 0.5, 1.0), dt);
            return naive;
        }

        case OSC_SAWTOOTH: {
            double naive = (osc->phase * 2.0) - 1.0;
            // Apply correction at the wrap-around (1.0 back to 0.0)
            return naive - poly_blep(osc->phase, dt);
        }

        case OSC_TRIANGLE: {
            // Triangle is already fairly clean (1/f^2), so we keep the naive version
            double val = (osc->phase < 0.5) ? (osc->phase * 2.0) : (2.0 - (osc->phase * 2.0));
            return (val * 2.0) - 1.0;
        }
        
        default: return 0.0;
    }
}

void osc_fill_buffer(Oscillator *osc, int16_t *buffer, int frames) {
    const double amplitude = 32767.0;

    for (int i = 0; i < frames; i++) {
        double val = osc_next_value(osc);
        int16_t sample = (int16_t)(val * amplitude);

        // Stereo Interleaving (Mono-to-Stereo)
        buffer[i * 2]     = sample; // Left
        buffer[i * 2 + 1] = sample; // Right
    }
}