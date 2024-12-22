#include <stdio.h>
#include <stdlib.h>

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

typedef unsigned long uintptr_t;

void calculate_machine_code(uintptr_t value, unsigned int register_number, unsigned char* machine_code_array)
{
    // for movz
    unsigned long opcode_movz = 0xd2800000 + register_number;
    
    // for movk
    unsigned long opcode_movk = 0xf2800000 + register_number;
    
    // 最初の 16 ビットを取り出す
    unsigned long first_value = value & 0xffff;
    
    printf("First Value: 0x%lx -- ", first_value);
    print_binary(first_value);
    
    // 即値部分にいれるために左にオフセット
    unsigned long first_result = opcode_movz + (first_value << 5);
    
    for (int i = 0; i < 4; i++)
    {
        unsigned int tmp = (first_result >> (8 * i)) & 0xff;
        printf("%d: %x\n", i, tmp);
        machine_code_array[i] = tmp;
    }
    
    // 残りの部分を movk で設定    
    for (unsigned int i = 0; i < 3; i++)
    {
        printf("-------------\n");
        
        unsigned long target_value = ((unsigned long long)value >> ((i + 1) * 16)) & 0xffff;
        printf("Target Value: 0x%lx -- ", target_value);
        print_binary(target_value);
        
        unsigned long target_result = opcode_movk + (target_value << 5);
        target_result += (i + 1) << 21;
        
        for (int j = 0; j < 4; j++)
        {
            unsigned int tmp = (target_result >> (8 * j)) & 0xff;
            int index = ((i + 1) * 4 + j);
            printf("%d: %x\n", index, tmp);
            machine_code_array[index] = tmp;
        }
    }
}

int main(int argc, char* argv[])
{
    if (argc == 1)
    {
        fprintf(stderr, "Usage: %s <hex value>\n", argv[0]);
        return 1;
    }

    // 4 命令文を格納
    unsigned char shell_code[16] = {
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
    };
    
    unsigned long target = (unsigned long)strtol(argv[1], NULL, 16);
    printf("Calculating value is 0x%lx\n", target);
    
    calculate_machine_code(target, 0, shell_code);
    
    printf("=============================\n");
    
    unsigned long decoded = 0;
    for (int i = 0; i < 4; i++)
    {
        unsigned long result = 0;
        for (int j = 0; j < 4; j++)
        {
            int index = (i * 4) + j;
            result += ((unsigned long)shell_code[index] << (8 * j));
        }
        printf("%d: %lx -- ", i, result);
        print_binary(result);
        
        unsigned long tmp = ((result >> 5) & 0xffff) << (i * 16);
        printf("    0x%lx\n", tmp);
        decoded += tmp;
    }
    
    printf("Decoded: 0x%lx\n", decoded);
    
    if (decoded == target)
    {
        printf("Match!!\n");
    }
}