#include <stdio.h>

extern void print_binary(unsigned long value);
extern void calculate_machine_code(uintptr_t value, unsigned int register_number, unsigned char* machine_code_array);

void print_binary(unsigned long value)
{
    unsigned int size = sizeof(unsigned long);
    unsigned int bits = size * 8;
    
    for (int i = bits - 1; i >= 0; i--)
    {
        unsigned int v = (value >> i) & 0x1;
        if (v == 0)
        {
            printf("0");
        }
        else
        {
            printf("1");
        }
    }
    printf("\n");
}

void calculate_machine_code(uintptr_t value, unsigned int register_number, unsigned char* machine_code_array)
{
    // for movz
    unsigned long opcode_movz = 0xd2800000 + register_number;
    
    // for movk
    unsigned long opcode_movk = 0xf2800000 + register_number;
    
    // 最初の 16 ビットを取り出す
    unsigned long first_value = value & 0xffff;
    
    // 即値部分にいれるために左にオフセット
    unsigned long first_result = opcode_movz + (first_value << 5);
    
    for (int i = 0; i < 4; i++)
    {
        unsigned int tmp = first_result >> ((3 - i) * 8) & 0xff;
        machine_code_array[i] = tmp;
    }
    
    // 残りの部分を movk で設定    
    for (unsigned int i = 0; i < 3; i++)
    {
        unsigned long target_value = ((unsigned long long)value >> ((i + 1) * 16)) & 0xffff;
        
        unsigned long target_result = opcode_movk + (target_value << 5);
        target_result += (i + 1) << 21;
        
        for (int j = 0; j < 4; j++)
        {
            unsigned int tmp = target_result >> ((3 - j) * 8) & 0xff;
            int index = ((i + 1) * 4 + j);
            machine_code_array[index] = tmp;
        }
    }
}