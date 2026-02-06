#ifndef ALSA_OUTPUT_H
#define ALSA_OUTPUT_H

#include <alsa/asoundlib.h>
#include <stdint.h>

typedef struct {
    snd_pcm_t *handle;
    char *device_name;
    unsigned int sample_rate;
    int channels;
} AlsaDevice;

// Opens the device and configures Hardware Parameters
int alsa_device_open(AlsaDevice *dev, const char *name, unsigned int rate, int channels);

// Closes the device
void alsa_device_close(AlsaDevice *dev);

// Writes interleaved S16_LE frames to the hardware
snd_pcm_sframes_t alsa_device_write(AlsaDevice *dev, int16_t *buffer, int frames);

#endif