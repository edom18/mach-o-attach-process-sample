#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <mach-o/dyld.h>

#define BUFFER_SIZE 100

int main()
{
    char input[BUFFER_SIZE];

    printf("Please input any text. Exit to type input 'exit'\n");

    printf("----\n");

    pid_t pid = getpid();
    printf("pid is [%d]\n", pid);

    // void* libsample = dlopen("./libsample.dylib", 2);

    while (1)
    {
        printf("Input: ");
        if (fgets(input, BUFFER_SIZE, stdin) != NULL)
        {
            input[strcspn(input, "\n")] = '\0';

            if (strcmp(input, "exit") == 0)
            {
                printf("Exiting.\n");
                break;
            }
            else if (strcmp(input, "list") == 0)
            {
                printf("Listing symbols...\n");

                uint32_t image_count = _dyld_image_count();
                printf("Dynamic image count: %d\n", image_count);

                for (uint32_t i = 0; i < image_count; i++)
                {
                    const char* name = _dyld_get_image_name(i);
                    printf("Lib name: %s\n", name);
                }
            }
            else if (strcmp(input, "pid") == 0)
            {
                void (*print_pid_fn)() = dlsym(RTLD_DEFAULT, "print_pid");
                if (print_pid_fn)
                {
                    printf("Printing PID...\n");
                    print_pid_fn();
                }
                else
                {
                    printf("print_pid function is not found.\n");
                }
            }
            else
            {
                printf("Input text: %s\n", input);
            }
        }
        else
        {
            printf("Error was happend\n");
            break;
        }
    }

    return 0;
}