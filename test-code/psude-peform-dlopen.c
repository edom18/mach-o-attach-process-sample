#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/task_info.h>
#include <mach/mach_vm.h>
#include <mach-o/loader.h>
#include <mach-o/dyld_images.h>
#include <mach-o/nlist.h>

extern void calculate_machine_code(uintptr_t value, unsigned int register_number, unsigned char* machine_code_array);

void wait_input();

int main()
{
    kern_return_t kr;
    mach_port_t task = mach_task_self();

    // シェルコードを仕込む
    // まずは、対象プロセス内にライブラリパス文字列を配置する
    const char* lib_path = "../libsample.dylib";
    size_t path_size = strlen(lib_path) + 1;

    mach_vm_address_t path_addr = 0;
    kr = mach_vm_allocate(task, &path_addr, path_size, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "Failed to allocate for lib path memory.\n");
        return 1;
    }

    kr = vm_protect(task, path_addr, path_size, FALSE, VM_PROT_READ | VM_PROT_WRITE);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "Failed to change protect mode for lib path memory.\n");
        return 1;
    }

    kr = mach_vm_write(task, path_addr, (vm_offset_t)lib_path, (mach_msg_type_number_t)path_size);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "Failed to write lib path to the process.\n");
        return 1;
    }

    uintptr_t path_address_value = (uintptr_t)path_addr;
    printf("[DEBUG] Allocated path memory: %p\n", (void*)path_addr);

    // シェルコード
    // アセンブリ:
    //      ; x0 = path_address
    //      movz x0, #0x****
    //      movk x0, #0x****, LSL #16
    //      movk x0, #0x****, LSL #32
    //      movk x0, #0x****, LSL #48
    //
    //      ; x1 = RTLD_NOW (2)
    //      movz x1, #2
    //
    //      ; x16 = dlopen_address
    //      movz x16, #0x****
    //      movk x16, #0x****, LSL #16
    //      movk x16, #0x****, LSL #32
    //      movk x16, #0x****, LSL #48
    //
    //      ; 関数呼び出し
    //      blr x16

    // 第一引数の構築（ mov x0, char_address )
    unsigned char shell_code_path_address[4 * 4]; // 4 命令文
    calculate_machine_code(path_address_value, 0, shell_code_path_address);
    size_t shellcode_path_address_size = sizeof(shell_code_path_address);

    // 第二引数の構築（ mov x1, #2 )
    // movz 110100101 00 0000000000000010 00001
    //      11010010 10000000 00000000 01000001
    //      0xd2     0x80     0x00     0x41 // 16 進数表記
    // これをリトルエンディアンで並べる
    unsigned char shell_code_rtld[4] = { // 1 命令文
        0x41, 0x0, 0x80, 0xd2,
    };
    size_t shellcode_rtld_size = sizeof(shell_code_rtld);

    // dlopen のアドレスをレジスタに設定（ mov x16, dlopen_address ）
    unsigned char shell_code_dlopen_address[4 * 4]; // 4 命令文
    void* real_dlopen = dlsym(RTLD_DEFAULT, "dlopen");
    printf("[DEBUG] dlsym reported dlopen: %p\n", real_dlopen);
    calculate_machine_code((uintptr_t)real_dlopen, 16, shell_code_dlopen_address);
    size_t shellcode_dlopen_address_size = sizeof(shell_code_dlopen_address);

    for (int i = 0; i < 4; i++)
    {
        unsigned long result;
        for (int j = 0; j < 4; j++)
        {
            int index = (i * 4) + j;
            unsigned int tmp = (unsigned int)shell_code_dlopen_address[index];
            printf("%02x ", tmp);
            result += tmp << (8 * j);
        }
        printf("[Instruction %d] 0x%lx\n", i, result);
    }

    // 分岐命令（ blr x16 ）
    // 11010110 00111111 00000010 00000000
    // 0xd6     0x3f     0x02     0x00 // 16 進数表記
    // これをリトルエンディアンで並べる
    unsigned char shell_code_blr[4] = { // 1 命令文
        0x00, 0x02, 0x3f, 0xd6,
    };
    size_t shellcode_blr_address_size = sizeof(shell_code_blr);

    size_t whole_shellcode_size = shellcode_path_address_size + shellcode_rtld_size + shellcode_dlopen_address_size + shellcode_blr_address_size;
    printf("-------> shell code size: %lu\n", whole_shellcode_size);
    unsigned char* shell_code = (unsigned char*)malloc(whole_shellcode_size);
    if (!shell_code)
    {
        fprintf(stderr, "Failed to allocate memory for shell code.\n");
        return 1;
    }

    memcpy(shell_code, shell_code_path_address, shellcode_path_address_size);
    memcpy(shell_code + shellcode_path_address_size, shell_code_rtld, shellcode_rtld_size);
    memcpy(shell_code + shellcode_path_address_size + shellcode_rtld_size, shell_code_dlopen_address, shellcode_dlopen_address_size);
    memcpy(shell_code + shellcode_path_address_size + shellcode_rtld_size + shellcode_dlopen_address_size, shell_code_blr, shellcode_blr_address_size);

    // 命令文を出力してみる
    for (int i = 0; i < 10; i++)
    {
        unsigned long result = 0;
        for (int j = 0; j < 4; j++)
        {
            int index = (i * 4) + j;
            unsigned int tmp = (unsigned int)shell_code[index];
            printf("%02x ", tmp);
            result += tmp << (8 * j);
        }
        printf("%d: %lx\n", i, result);
    }

    // コード用メモリ領域確保
    mach_vm_address_t code_addr = 0;
    kr = mach_vm_allocate(task, &code_addr, whole_shellcode_size, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "Failed to allocate code memory on target process.\n");
        return 1;
    }
    printf("Allocated shell code memory address: %p\n", (void*)code_addr);

    // 書き込み・読み込み権限を付与
    kr = vm_protect(task, code_addr, whole_shellcode_size, FALSE, VM_PROT_READ | VM_PROT_WRITE);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "Failed to change protect attribute on code memory.\n");
        return 1;
    }

    // シェルコードの書き込み
    kr = mach_vm_write(task, code_addr, (vm_offset_t)shell_code, (mach_msg_type_number_t)whole_shellcode_size);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "Failed to write shell code on target process.\n");
        return 1;
    }

    // 実行権限付与に変更
    kr = vm_protect(task, code_addr, whole_shellcode_size, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "Failed to change shell code memory as executable.\n");
        return 1;
    }

    // スタック用メモリ領域確保
    mach_vm_address_t stack_addr = 0;
    vm_size_t stack_size = 1024 * 60;
    kr = mach_vm_allocate(task, &stack_addr, stack_size, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "Failed to allocate memory for stack.\n");
        return 1;
    }

    // スタック領域を読み書き可能に
    kr = vm_protect(task, stack_addr, stack_size, FALSE, VM_PROT_READ | VM_PROT_WRITE);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "Failed to change memory protect for stack memory.\n");
        return 1;
    }

    // スタックポインタはスタック末尾付近
    uint64_t sp = stack_addr + stack_size - 16;

    // スレッド作成
    thread_act_t new_thread;
    kr = thread_create(task, &new_thread);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "Failed to create a new thread on target process: %s\n", mach_error_string(kr));
        return 1;
    }

    // スレッド状態設定
    arm_thread_state64_t state;
    memset(&state, 0, sizeof(state));

    state.__pc = code_addr;
    state.__sp = sp;
    state.__fp = sp;

    mach_msg_type_number_t state_count = ARM_THREAD_STATE64_COUNT;
    kr = thread_set_state(new_thread, ARM_THREAD_STATE64, (thread_state_t)&state, state_count);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "Failed to set thread set: %s.\n", mach_error_string(kr));
        return 1;
    }

    // スレッド開始
    kr = thread_resume(new_thread);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "Failed to resume new thread: %s\n", mach_error_string(kr));
        return 1;
    }

    wait_input();
}

void wait_input()
{
    #define BUFFER_SIZE 100
    char input[BUFFER_SIZE];
    if (fgets(input, BUFFER_SIZE, stdin) != NULL)
    {
        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "exit") == 0)
        {
            printf("Exiting.\n");
            return;
        }
        else
        {
            printf("Input text: %s\n", input);
        }
    }
    else
    {
        printf("Error was happend\n");
        return;
    }
}