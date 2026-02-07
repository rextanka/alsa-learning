#ifndef ENVELOPE_H
#define ENVELOPE_H

#include <stdint.h>

/**
 * ADSR State Machine Definitions
 */
typedef enum {
    ENV_STATE_IDLE,      // Silence
    ENV_STATE_ATTACK,    // Gain ramping up
    ENV_STATE_DECAY,     // Gain dropping to sustain level
    ENV_STATE_SUSTAIN,   // Gain held constant
    ENV_STATE_RELEASE    // Gain dropping to zero
} env_state_t;

typedef struct {
    env_state_t state;
    double current_gain;
    double sample_rate;

    // Coefficients (Gain change per sample)
    double attack_step;
    double decay_mult;   // Multiplier for exponential decay
    double release_mult; // Multiplier for exponential release
    double sustain_level;
} adsr_t;

void adsr_init(adsr_t *env, double sample_rate);
void adsr_set_params(adsr_t *env, double a_sec, double d_sec, double s_lvl, double r_sec);
void adsr_note_on(adsr_t *env);
void adsr_note_off(adsr_t *env);
double adsr_process(adsr_t *env);

#endif