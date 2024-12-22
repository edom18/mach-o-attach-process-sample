#include <stdio.h>

extern void print_binary(unsigned long value);
extern void calculate_machine_code(uintptr_t value, unsigned int register_number, unsigned char* machine_code_array);

int main()
{
    uintptr_t address = 0x180451c04;
    unsigned char shell_code[4 * 4];
    calculate_machine_code(address, 0, shell_code);

    // 4 命令分を表示
    for (int i = 0; i < 4; i++)
    {
        unsigned long result = 0;
        for (int j = 0; j < 4; j++)
        {
            int index = (i * 4) + j;
            unsigned long tmp = (unsigned long)shell_code[index] << (8 * j);
            result += tmp;
        }
        printf("%d: %lx -- ", i, result);
        print_binary(result);
    }

    for (int i = 0; i < 16; i++)
    {
        if (i % 4 == 0)
        {
            printf("\n");
        }

        printf("0x%x ", shell_code[i]);
    }

    return 0;
}