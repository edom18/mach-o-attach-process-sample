#include <stdio.h>
#include <unistd.h>

__attribute__((constructor))
void initialize()
{
    printf("Initializing sample library...\n");

    pid_t pid = getpid();
    printf("Current pid in sample libyrar: %d\n", pid);
}