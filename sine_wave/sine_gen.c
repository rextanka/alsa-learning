#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include "oscillator.h"
#include "alsa_output.h"

// Buffer size for each ALSA write operation (frames)
#define BUFFER_FRAMES 1024
// Standard high-quality sample rate for Razer/ALC298 hardware
#define DEFAULT_RATE 48000

void print_help(char *prog_name) {
    printf("Sine Wave Generator for ALSA (Synthesizer & Test Gear Edition)\n");
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -f <float>    Starting Frequency in Hz (default: 1000.0)\n");
    printf("  -t <float>    Target Frequency in Hz (enables Sweep/Glide mode)\n");
    printf("  -d <float>    Total playback duration in seconds (default: 3.0)\n");
    printf("  -g <float>    Glide (Portamento) time in seconds\n");
    printf("                If omitted with -t, duration (-d) is used for a full sweep.\n");
    printf("  -o <string>   ALSA device name (default: default)\n");
    printf("  -h            Show this help message\n");
}

int main(int argc, char *argv[]) {
    int opt;
    
    /* --- Parameter Defaults --- */
    double freq = 1000.0;
    double target_freq = -1.0; // Negative indicates no target set
    double duration = 3.0;
    double glide_time = -1.0;  // Negative indicates no specific glide time
    char *device_name = "default";

    /* --- Command Line Parsing --- */
    while ((opt = getopt(argc, argv, "f:t:d:g:o:h")) != -1) {
        switch (opt) {
            case 'f': freq = atof(optarg); break;
            case 't': target_freq = atof(optarg); break;
            case 'd': duration = atof(optarg); break;
            case 'g': glide_time = atof(optarg); break;
            case 'o': device_name = optarg; break;
            case 'h': 
                print_help(argv[0]); 
                return 0;
            default: 
                return 1;
        }
    }

    /* 1. Initialize ALSA Device 
       Targeting Stereo, 48kHz, S16_LE (standard for the Razer Laptop profile) */
    AlsaDevice alsa;
    if (alsa_device_open(&alsa, device_name, DEFAULT_RATE, 2) < 0) {
        fprintf(stderr, "Failed to initialize ALSA device: %s\n", device_name);
        return 1;
    }

    /* 2. Initialize Oscillator 
       The oscillator starts at the initial 'freq' and uses the hardware sample rate */
    Oscillator osc;
    osc_init(&osc, OSC_SINE, freq, alsa.sample_rate);

    /* 3. Configure Frequency Transition Logic 
       Logic: If a target frequency (-t) is provided, we initiate a ramp.
       If glide time (-g) is missing, we use the total duration (-d) for a full sweep. */
    if (target_freq > 0) {
        double transition_time = (glide_time > 0) ? glide_time : duration;
        
        osc_set_target(&osc, target_freq, transition_time);
        
        printf("Mode: %s\n", (glide_time > 0) ? "GLIDE (Portamento)" : "SWEEP (Linear)");
        printf("Ramping: %.2fHz -> %.2fHz over %.2fs (Total Playtime: %.2fs)\n", 
               freq, target_freq, transition_time, duration);
    } else {
        printf("Mode: STATIC TONE\n");
        printf("Playing %.2fHz tone for %.2fs on device '%s'...\n", 
               freq, duration, device_name);
    }

    /* 4. Main Playback Loop 
       We process audio in chunks of BUFFER_FRAMES to keep latency low and efficiency high */
    int16_t buffer[BUFFER_FRAMES * 2]; // Interleaved Stereo (Left/Right)
    long total_frames = (long)(duration * alsa.sample_rate);
    long frames_played = 0;

    while (frames_played < total_frames) {
        int frames_to_play = BUFFER_FRAMES;
        
        // Final buffer truncation check
        if (frames_played + frames_to_play > total_frames) {
            frames_to_play = total_frames - frames_played;
        }

        // DSP: Fill the local buffer with oscillator samples
        osc_fill_buffer(&osc, buffer, frames_to_play);
        
        // Hardware: Write the buffer to the soundcard
        if (alsa_device_write(&alsa, buffer, frames_to_play) < 0) {
            fprintf(stderr, "ALSA write error occurred. Exiting loop.\n");
            break;
        }

        frames_played += frames_to_play;
    }

    /* 5. Clean up resources */
    alsa_device_close(&alsa);
    printf("Playback finished successfully.\n");

    return 0;
}