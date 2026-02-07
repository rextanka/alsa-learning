#include "alsa_output.h"
#include <stdio.h>

int alsa_device_open(AlsaDevice *dev, const char *name, unsigned int rate, int channels) {
    int err;
    snd_pcm_hw_params_t *params;

    // 1. Open PCM Tool in Playback mode
    if ((err = snd_pcm_open(&dev->handle, name, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        fprintf(stderr, "Cannot open audio device %s (%s)\n", name, snd_strerror(err));
        return err;
    }

    // 2. Allocate hw_params structure and fill with default values
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(dev->handle, params);

    // 3. Set Hardware Parameters
    // Access: Interleaved (LRLR...)
    snd_pcm_hw_params_set_access(dev->handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    // Format: Signed 16-bit Little Endian
    snd_pcm_hw_params_set_format(dev->handle, params, SND_PCM_FORMAT_S16_LE);
    // Channels: Stereo
    snd_pcm_hw_params_set_channels(dev->handle, params, channels);
    // Rate: 48000 (usually)
    snd_pcm_hw_params_set_rate_near(dev->handle, params, &rate, 0);

    // 4. Apply parameters to the hardware
    if ((err = snd_pcm_hw_params(dev->handle, params)) < 0) {
        fprintf(stderr, "Cannot set hardware parameters (%s)\n", snd_strerror(err));
        return err;
    }

    dev->device_name = strdup(name);
    dev->sample_rate = rate;
    dev->channels = channels;

    return 0;
}

void alsa_device_close(AlsaDevice *dev) {
    if (dev->handle) {
        snd_pcm_drain(dev->handle);
        snd_pcm_close(dev->handle);
        free(dev->device_name);
    }
}

snd_pcm_sframes_t alsa_device_write(AlsaDevice *dev, int16_t *buffer, int frames) {
    snd_pcm_sframes_t written = snd_pcm_writei(dev->handle, buffer, frames);
    
    // Handle Underrun (Buffer empty)
    if (written == -EPIPE) {
        snd_pcm_prepare(dev->handle);
    } else if (written < 0) {
        fprintf(stderr, "Error writing to PCM device: %s\n", snd_strerror(written));
    }
    return written;
}