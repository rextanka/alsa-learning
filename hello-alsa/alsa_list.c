#include <stdio.h>
#include <alsa/asoundlib.h>

int main() {
    void **hints;
    char **n;
    char *name, *descr, *ioid;

    // Get the list of PCM device hints
    if (snd_device_name_hint(-1, "pcm", &hints) < 0) {
        fprintf(stderr, "Error: Unable to get device hints\n");
        return 1;
    }

    n = (char **)hints;
    printf("=== Available ALSA PCM Devices ===\n");

    while (*n != NULL) {
        name  = snd_device_name_get_hint(*n, "NAME");
        descr = snd_device_name_get_hint(*n, "DESC");
        ioid  = snd_device_name_get_hint(*n, "IOID");

        printf("Device: %s\n", name ? name : "N/A");
        if (descr) printf("  Description: %s\n", descr);
        if (ioid)  printf("  Direction:   %s\n", ioid);
        printf("----------------------------------\n");

        if (name)  free(name);
        if (descr) free(descr);
        if (ioid)  free(ioid);
        n++;
    }

    snd_device_name_free_hint(hints);
    return 0;
}