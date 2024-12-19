#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BUFFER_SIZE 100

int main()
{
    char input[BUFFER_SIZE];

    printf("Please input any text. Exit to type input 'exit'\n");

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