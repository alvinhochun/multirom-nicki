#include <stdio.h>
#include <stdlib.h>
#include <cutils/android_reboot.h>

int main(int argc, char *argv[])
{
    if(argc > 2) {
        fprintf(stderr, "%s: too many arguments\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    if(argc == 2 && strcmp(argv[1], "--help") == 0) {
        fprintf(stderr, "usage: %s [command]\n", argv[0]);
        return 0;
    }

    if(argc == 2)
        android_reboot(ANDROID_RB_RESTART2, 0, argv[1]);
    else
        android_reboot(ANDROID_RB_RESTART, 0, NULL);

    fprintf(stderr, "failed to reboot\n");
    return EXIT_FAILURE;
}
