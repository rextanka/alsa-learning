#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include "oscillator.h"
#include "alsa_output.h"

// Buffer size for each ALSA write operation (in frames)
#define BUFFER_FRAMES 1024
// Default sample rate (48kHz matches the Razer Laptop hardware profile)
#define DEFAULT_RATE 48000

void print_help(char *prog_name) {
    printf("ALSA Waveform Generator (Synthesizer & Test Gear Edition)\n");
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -f <float>    Starting Frequency in Hz (default: 1000.0)\n");
    printf("  -t <float>    Target Frequency in Hz (enables Sweep/Glide mode)\n");
    printf("  -d <float>    Total playback duration in seconds (default: 3.0)\n");
    printf("  -g <float>    Glide (Portamento) time in seconds (sweep duration if omitted)\n");
    printf("  -w <string>   Waveform: sine, square, triangle, saw (default: sine)\n");
    printf("  -o <string>   ALSA device name (default: default)\n");
    printf("  -h            Show this help message\n");
}

int main(int argc, char *argv[]) {
    int opt;
    
    // --- Parameter Defaults ---
    double freq = 1000.0;
    double target_freq = -1.0; 
    double duration = 3.0;
    double glide_time = -1.0;
    OscillatorType type = OSC_SINE;
    char *device_name = "default";
    char *waveform_name = "sine";

    // --- Command Line Parsing ---
    // Added 't', 'g', and 'w' to handle ramping and waveform selection
    while ((opt = getopt(argc, argv, "f:t:d:g:w:o:h")) != -1) {
        switch (opt) {
            case 'f': freq = atof(optarg); break;
            case 't': target_freq = atof(optarg); break;
            case 'd': duration = atof(optarg); break;
            case 'g': glide_time = atof(optarg); break;
            case 'w':
                waveform_name = optarg;
                if (strcmp(optarg, "square") == 0) type = OSC_SQUARE;
                else if (strcmp(optarg, "triangle") == 0) type = OSC_TRIANGLE;
                else if (strcmp(optarg, "saw") == 0) type = OSC_SAWTOOTH;
                else type = OSC_SINE; // Default fallback
                break;
            case 'o': device_name = optarg; break;
            case 'h': print_help(argv[0]); return 0;
            default: return 1;
        }
    }

    // 1. Initialize ALSA Device (Stereo, 48kHz, S16_LE)
    AlsaDevice alsa;
    if (alsa_device_open(&alsa, device_name, DEFAULT_RATE, 2) < 0) {
        fprintf(stderr, "Fatal: Could not open ALSA device %s\n", device_name);
        return 1;
    }

    // 2. Initialize Oscillator
    // Now uses 'double' for frequency to support high-precision sweeps
    Oscillator osc;
    osc_init(&osc, type, freq, alsa.sample_rate);

    // 3. Configure Ramp/Transition Logic
    // If a target frequency (-t) is provided, determine if it's a glide or a full sweep.
    if (target_freq > 0) {
        double transition_time = (glide_time > 0) ? glide_time : duration;
        osc_set_target(&osc, target_freq, transition_time);
        
        printf("Ramping %s: %.2fHz -> %.2fHz over %.2fs (Total: %.2fs)\n", 
               waveform_name, freq, target_freq, transition_time, duration);
    } else {
        printf("Playing static %.2fHz %s wave for %.2fs on '%s'...\n", 
               freq, waveform_name, duration, device_name);
    }

    // 4. Playback Loop
    int16_t buffer[BUFFER_FRAMES * 2]; // Interleaved Stereo buffer
    long total_frames = (long)(duration * alsa.sample_rate);
    long frames_played = 0;

    while (frames_played < total_frames) {
        int frames_to_play = BUFFER_FRAMES;
        
        // Truncate last buffer to match exact duration requested
        if (frames_played + frames_to_play > total_frames) {
            frames_to_play = total_frames - frames_played;
        }

        // Fill buffer with current waveform samples
        osc_fill_buffer(&osc, buffer, frames_to_play);
        
        // Send to hardware
        if (alsa_device_write(&alsa, buffer, frames_to_play) < 0) {
            fprintf(stderr, "ALSA write error occurred. Exiting.\n");
            break;
        }

        frames_played += frames_to_play;
    }

    // 5. Cleanup
    alsa_device_close(&alsa);
    printf("Playback finished.\n");

    return 0;
}