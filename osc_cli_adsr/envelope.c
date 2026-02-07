#include "envelope.h"
#include <math.h>

void adsr_init(adsr_t *env, double sample_rate) {
    env->sample_rate = sample_rate;
    env->state = ENV_STATE_IDLE;
    env->current_gain = 0.0;
    // Default musical settings: 10ms Attack, 100ms Decay, 0.7 Sustain, 200ms Release
    adsr_set_params(env, 0.01, 0.1, 0.7, 0.2);
}

void adsr_set_params(adsr_t *env, double a_sec, double d_sec, double s_lvl, double r_sec) {
    // Prevent division by zero or negative times with a 1ms floor
    if (a_sec <= 0) a_sec = 0.001;
    if (d_sec <= 0) d_sec = 0.001;
    if (r_sec <= 0) r_sec = 0.001;

    // ENHANCEMENT: Linear Attack
    // Calculate gain increment per sample to reach 1.0 in a_sec
    env->attack_step = 1.0 / (a_sec * env->sample_rate);

    // ENHANCEMENT: Exponential Coefficients
    // We use a 1-pole filter coefficient to create a natural curved drop-off
    env->decay_mult = 1.0 - exp(-1.0 / (d_sec * env->sample_rate));
    env->release_mult = 1.0 - exp(-1.0 / (r_sec * env->sample_rate));
    
    env->sustain_level = s_lvl;
}

void adsr_note_on(adsr_t *env) {
    // ENHANCEMENT: Legato Triggering
    // We do NOT reset current_gain to 0.0 here. If a previous note is still 
    // releasing, the new attack starts from the current level to avoid "pops".
    env->state = ENV_STATE_ATTACK;
}

void adsr_note_off(adsr_t *env) {
    env->state = ENV_STATE_RELEASE;
}

double adsr_process(adsr_t *env) {
    switch (env->state) {
        case ENV_STATE_ATTACK:
            env->current_gain += env->attack_step;
            if (env->current_gain >= 1.0) {
                env->current_gain = 1.0;
                env->state = ENV_STATE_DECAY;
            }
            break;

        case ENV_STATE_DECAY:
            // ENHANCEMENT: Exponential Decay toward Sustain Level
            env->current_gain += (env->sustain_level - env->current_gain) * env->decay_mult;
            
            // Threshold check to snap to sustain state
            if (fabs(env->current_gain - env->sustain_level) < 0.0001) {
                env->current_gain = env->sustain_level;
                env->state = ENV_STATE_SUSTAIN;
            }
            break;

        case ENV_STATE_SUSTAIN:
            // ENHANCEMENT: Parameter Slewing
            // If the user changes sustain_level via CLI, we slowly drift to it 
            // rather than jumping, preventing zipper noise.
            env->current_gain += (env->sustain_level - env->current_gain) * 0.005;
            break;

        case ENV_STATE_RELEASE:
            // ENHANCEMENT: Exponential Release toward Zero
            env->current_gain += (0.0 - env->current_gain) * env->release_mult;
            
            if (env->current_gain < 0.0001) {
                env->current_gain = 0.0;
                env->state = ENV_STATE_IDLE;
            }
            break;

        default:
            env->current_gain = 0.0;
            break;
    }
    return env->current_gain;
}