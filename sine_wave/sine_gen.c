#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include "oscillator.h"
#include "alsa_output.h"

#define BUFFER_FRAMES 1024
#define DEFAULT_RATE 48000

void print_help(char *prog_name) {
    printf("Sine Wave Generator for ALSA\n");
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -f <int>      Frequency in Hz (default: 1000)\n");
    printf("  -d <float>    Duration in seconds (default: 3.0)\n");
    printf("  -o <string>   ALSA device name (default: default)\n");
    printf("  -h            Show this help message\n");
}

int main(int argc, char *argv[]) {
    int opt;
    int freq = 1000;
    double duration = 3.0;
    char *device_name = "default";
    
    bool freq_provided = false;
    bool dur_provided = false;
    bool dev_provided = false;

    while ((opt = getopt(argc, argv, "f:d:o:h")) != -1) {
        switch (opt) {
            case 'f':
                freq = atoi(optarg);
                freq_provided = true;
                break;
            case 'd':
                duration = atof(optarg);
                dur_provided = true;
                break;
            case 'o':
                device_name = optarg;
                dev_provided = true;
                break;
            case 'h':
                print_help(argv[0]);
                return 0;
            default:
                return 1;
        }
    }

    // 1. Initialize ALSA Device
    AlsaDevice alsa;
    if (alsa_device_open(&alsa, device_name, DEFAULT_RATE, 2) < 0) {
        return 1;
    }

    // 2. Initialize Oscillator
    Oscillator osc;
    osc_init(&osc, OSC_SINE, freq, alsa.sample_rate);

    // 3. Status Output
    printf("Playing %dHz %s sine wave on '%s' %s for %.2fs %s...\n",
           freq, freq_provided ? "" : "(default)",
           device_name, dev_provided ? "" : "(default)",
           duration, dur_provided ? "" : "(default)");

    // 4. Playback Loop
    int16_t buffer[BUFFER_FRAMES * 2]; // Stereo buffer
    long total_frames = (long)(duration * alsa.sample_rate);
    long frames_played = 0;

    while (frames_played < total_frames) {
        int frames_to_play = BUFFER_FRAMES;
        
        // Ensure we don't play past the requested duration
        if (frames_played + frames_to_play > total_frames) {
            frames_to_play = total_frames - frames_played;
        }

        osc_fill_buffer(&osc, buffer, frames_to_play);
        
        if (alsa_device_write(&alsa, buffer, frames_to_play) < 0) {
            break; // Exit on fatal write error
        }

        frames_played += frames_to_play;
    }

    // 5. Cleanup
    alsa_device_close(&alsa);
    printf("Playback finished.\n");

    return 0;
}