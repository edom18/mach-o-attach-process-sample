#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach.h>
#include <mach/thread_act.h>
#include <mach/mach_vm.h>
#include <mach/arm/thread_state.h>
#include <unistd.h>

int main(void)
{
    kern_return_t kr;
    mach_port_t task = mach_task_self();

    // シェルコード（w0 = 42; 無限ループ）
    // アセンブリ: mov w0, #42; b .
    unsigned char shellcode[] = {
        0x2a, 0x05, 0x80, 0x52, // mov w0, #42 -> 0x5280052a
        0x00, 0x00, 0x00, 0x14, // b . -> 0x14000000
    };
    size_t shellcode_size = sizeof(shellcode);

    // 1. コード用メモリ領域確保
    mach_vm_address_t code_addr = 0;
    kr = mach_vm_allocate(task, &code_addr, shellcode_size, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "mach_vm_allocate for code failed: %s\n", mach_error_string(kr));
        return 1;
    }

    // 書き込み・読み込み権限付与
    kr = vm_protect(task, code_addr, shellcode_size, FALSE, VM_PROT_READ | VM_PROT_WRITE);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "vm_protect for code failed: %s\n", mach_error_string(kr));
        return 1;
    }

    // 2. シェルコード書き込み
    // (vm_offset_t)shellcode は、データの開始アドレス
    kr = mach_vm_write(task, code_addr, (vm_offset_t)shellcode, (mach_msg_type_number_t)shellcode_size);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "mach_vm_write failed: %s\n", mach_error_string(kr));
        return 1;
    }

    // 2-1. WRITE を外して EXECUTE 権限を付与（READ | EXECUTE）
    kr = vm_protect(task, code_addr, shellcode_size, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "vm_protect to RX failed: %s\n", mach_error_string(kr));
        return 1;
    }

    // 3. スタック用メモリ領域確保
    mach_vm_address_t stack_addr = 0;
    vm_size_t stack_size = 4096;
    kr = mach_vm_allocate(task, &stack_addr, stack_size, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "mach_vm_allocate for stack failed: %s\n", mach_error_string(kr));
        return 1;
    }

    // スタックを読み書き可能に
    kr = vm_protect(task, stack_addr, stack_size, FALSE, VM_PROT_READ | VM_PROT_WRITE);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "vm_protect for stack failed: %s\n", mach_error_string(kr));
        return 1;
    }

    // スタックポインタはスタック末尾付近
    uint64_t sp = stack_addr + stack_size - 16;

    // 4. スレッド作成
    thread_act_t new_thread;
    kr = thread_create(task, &new_thread);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "thread_create failed: %s\n", mach_error_string(kr));
        return 1;
    }

    // 5. スレッド状態設定（ARM64）
    arm_thread_state64_t state;
    memset(&state, 0, sizeof(state));

    // __pc: プログラムカウンタ
    // __sp: スタックポインタ
    // __fp: フレームポインタ（必須ではないが一応設定）
    state.__pc = code_addr;
    state.__sp = sp;
    state.__fp = sp; // 適当に設定

    mach_msg_type_number_t state_count = ARM_THREAD_STATE64_COUNT;
    kr = thread_set_state(new_thread, ARM_THREAD_STATE64, (thread_state_t)&state, state_count);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "thread_set_state failed: %s\n", mach_error_string(kr));
        return 1;
    }

    // 6. スレッド実行開始
    kr = thread_resume(new_thread);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "thread_resume failed: %s\n", mach_error_string(kr));
        return 1;
    }

    printf("Shellcode thread started on ARM64. W0=42 and infinite loop.\n");
    printf("Check CPU usage. Press Crrl+C to quit.\n");

    // メインスレッドは無限待ち
    for (;;)
    {
        sleep(1);
    }

    return 0;
}