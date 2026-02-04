#include <stdio.h>
#include <alsa/asoundlib.h>

int main() {
    // Standard ALSA version query
    printf("ALSA library version: %s\n", SND_LIB_VERSION_STR);

    return 0;
}