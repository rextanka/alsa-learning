#include <stdio.h>
#include <alsa/asoundlib.h>

int main() {
    int err;
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;
    unsigned int min_rate, max_rate;
    int dir;

    // We target the "front" device we found in alsa_list
    const char *device = "front:CARD=PCH,DEV=0";

    printf("Probing device: %s\n", device);

    if ((err = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        printf("Playback open error: %s\n", snd_strerror(err));
        return 1;
    }

    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(handle, params);

    // Query Rates
    snd_pcm_hw_params_get_rate_min(params, &min_rate, &dir);
    snd_pcm_hw_params_get_rate_max(params, &max_rate, &dir);
    printf("Rate range: %uHz - %uHz\n", min_rate, max_rate);

    // Query Channels
    unsigned int min_chan, max_chan;
    snd_pcm_hw_params_get_channels_min(params, &min_chan);
    snd_pcm_hw_params_get_channels_max(params, &max_chan);
    printf("Channels: %u to %u\n", min_chan, max_chan);

    // Check for common formats
    printf("Supported Formats:\n");
    if (snd_pcm_hw_params_test_format(handle, params, SND_PCM_FORMAT_S16_LE) == 0)
        printf(" - S16_LE (16-bit Little Endian)\n");
    if (snd_pcm_hw_params_test_format(handle, params, SND_PCM_FORMAT_S24_LE) == 0)
        printf(" - S24_LE (24-bit Little Endian)\n");
    if (snd_pcm_hw_params_test_format(handle, params, SND_PCM_FORMAT_S32_LE) == 0)
        printf(" - S32_LE (32-bit Little Endian)\n");

    snd_pcm_close(handle);
    return 0;
}