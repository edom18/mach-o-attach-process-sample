#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>

int main()
{
    printf("Hello!\n");

    void* lib_handle = dlopen("./libsample.dylib", 2);
    if (lib_handle == 0)
    {
        printf("Failed to open libsample\n");
        return 1;
    }

    pid_t pid = getpid();
    printf("Process pid: %d\n", pid);

    return 0;
}