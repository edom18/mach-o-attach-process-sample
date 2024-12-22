#include <stdio.h>

extern void print_binary(unsigned long value);
extern void calculate_machine_code(uintptr_t value, unsigned int register_number, unsigned char* machine_code_array);

int main()
{
    uintptr_t address = 0x180451c04;
    unsigned char shell_code[32 * 4];
    calculate_machine_code(address, 0, shell_code);

    for (int i = 0; i < 4; i++)
    {
        unsigned long result = 0;
        for (int j = 0; j < 4; j++)
        {
            int index = (i * 4) + j;
            result += ((unsigned long)shell_code[index] << (8 * (3 - j)));
        }
        printf("%d: %lx -- ", i, result);
        print_binary(result);
    }

    return 0;
}