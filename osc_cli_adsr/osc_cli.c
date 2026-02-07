#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include "oscillator.h"
#include "alsa_output.h"
#include "envelope.h"

#define BUFFER_FRAMES 1024
#define DEFAULT_RATE 48000

void print_help(char *prog_name) {
    printf("ALSA ADSR Synthesizer CLI\n");
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -f <float>    Frequency in Hz (default: 1000.0)\n");
    printf("  -d <float>    Total duration (calculated automatically if omitted)\n");
    printf("  -w <string>   Waveform: sine, square, triangle, saw (default: sine)\n");
    printf("  -A <float>    Attack time in seconds (default: 0.01)\n");
    printf("  -D <float>    Decay time in seconds (default: 0.1)\n");
    printf("  -S <float>    Sustain level 0.0-1.0 (default: 0.7)\n");
    printf("  -R <float>    Release time in seconds (default: 0.2)\n");
    printf("  -h            Show this help message\n");
}

int main(int argc, char *argv[]) {
    int opt;
    
    // Default Values
    double freq = 1000.0;
    double duration = -1.0; // Mark as unset
    OscillatorType type = OSC_SINE;
    char *waveform_name = "sine";

    double attack = 0.01, decay = 0.1, sustain = 0.7, release = 0.2;

    // Parse command line - ensures lowercase 'd' (duration) and uppercase 'D' (decay) coexist
    while ((opt = getopt(argc, argv, "f:d:w:A:D:S:R:h")) != -1) {
        switch (opt) {
            case 'f': freq = atof(optarg); break;
            case 'd': duration = atof(optarg); break;
            case 'w':
                waveform_name = optarg;
                if (strcmp(optarg, "square") == 0) type = OSC_SQUARE;
                else if (strcmp(optarg, "triangle") == 0) type = OSC_TRIANGLE;
                else if (strcmp(optarg, "saw") == 0) type = OSC_SAWTOOTH;
                break;
            case 'A': attack = atof(optarg); break;
            case 'D': decay = atof(optarg); break;
            case 'S': sustain = atof(optarg); break;
            case 'R': release = atof(optarg); break;
            case 'h': print_help(argv[0]); return 0;
            default: return 1;
        }
    }

    // --- Logical Timing Calculation ---
    double min_adr_time = attack + decay + release;
    
    // If no duration set, give a 1-second sustain by default
    if (duration < 0) {
        duration = min_adr_time + 1.0;
    } 
    // Safety check: ensure duration isn't shorter than the envelope's active movement
    else if (duration < min_adr_time) {
        printf("Note: Adjusting duration from %.2fs to %.2fs to fit ADSR cycle.\n", 
                duration, min_adr_time);
        duration = min_adr_time;
    }

    // 1. Initialize Hardware
    AlsaDevice alsa;
    if (alsa_device_open(&alsa, "default", DEFAULT_RATE, 2) < 0) return 1;

    // 2. Initialize Voice
    Oscillator osc;
    osc_init(&osc, type, freq, alsa.sample_rate);

    adsr_t env;
    adsr_init(&env, alsa.sample_rate);
    adsr_set_params(&env, attack, decay, sustain, release);
    
    adsr_note_on(&env); 
    
    printf("Playing %s: %.2fHz | Total: %.2fs (A:%.2f D:%.2f R:%.2f)\n", 
           waveform_name, freq, duration, attack, decay, release);

    // 3. Playback Loop
    int16_t buffer[BUFFER_FRAMES * 2];
    long total_frames = (long)(duration * alsa.sample_rate);
    long release_frame = total_frames - (long)(release * alsa.sample_rate);
    long frames_played = 0;
    bool released = false;

    while (frames_played < total_frames) {
        int to_play = (frames_played + BUFFER_FRAMES > total_frames) ? 
                      (total_frames - frames_played) : BUFFER_FRAMES;

        // Trigger release phase at the calculated moment
        if (!released && frames_played >= release_frame) {
            adsr_note_off(&env);
            released = true;
        }

        osc_fill_buffer(&osc, &env, buffer, to_play);
        
        if (alsa_device_write(&alsa, buffer, to_play) < 0) break;
        frames_played += to_play;
    }

    alsa_device_close(&alsa);
    return 0;
}