#include <stdio.h>
#include <unistd.h>

void print_pid()
{
    pid_t pid = getpid();
    printf("Current pid in sample libyrar: %d\n", pid);
}

__attribute__((constructor, visibility("default")))
void initialize()
{
    printf("Initializing sample library...\n");

    print_pid();
}