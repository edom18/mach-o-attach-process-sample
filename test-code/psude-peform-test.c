#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/thread_act.h>
#include <mach/mach_vm.h>
#include <mach/arm/thread_state.h>

extern void calculate_machine_code(uintptr_t value, unsigned int register_number, unsigned char* machine_code_array);

//--------------------------------------
// 1. 呼び出したい関数 (ターゲット)
//    今回は x+1 を返すだけ。
//--------------------------------------
int myfunc(int x)
{
    return x + 1;
}

//--------------------------------------
// メイン
//--------------------------------------
int main(void) {
    // まずターゲット関数のアドレスを取得
    uintptr_t myfunc_addr = (uintptr_t)&myfunc;

    // 2. シェルコードを作る
    //    movz x0, #42
    //    [movz+movk×3 で x16 = myfunc_addr]
    //    blr x16
    //    b .   (無限ループ)

    // 引数のセットアップはハードコードする
    unsigned char instr_movz_x0_42[4] = {
        0x2a, 0x02, 0x80, 0x52,
    };
    size_t instr_movz_x0_42_size = sizeof(instr_movz_x0_42);

    unsigned char instr_mov_x16[4 * 4];
    size_t instr_mov_x16_size = sizeof(instr_mov_x16);
    calculate_machine_code(myfunc_addr, 16, instr_mov_x16);

    // 命令文を出力してみる
    for (int i = 0; i < 4; i++)
    {
        unsigned long result = 0;
        for (int j = 0; j < 4; j++)
        {
            int index = (i * 4) + j;
            unsigned int tmp = (unsigned int)instr_mov_x16[index];
            printf("%02x ", tmp);
            result += tmp << (8 * j);
        }
        printf("%d: %lx\n", i, result);
    }

    // blr x16 -> 0xd63f0200
    unsigned char instr_blr_x16[4] = { // 1 命令文
        0x00, 0x02, 0x3f, 0xd6,
    };
    size_t instr_blr_x16_size = sizeof(instr_blr_x16);

    // b . -> 0x14000000
    unsigned char instr_b_loop[4] = {
        // 0x0, 0x0, 0x0, 0x14,
    };
    memset(instr_b_loop, 0, 4);
    instr_b_loop[3] = 0x14;
    size_t instr_b_loop_size = sizeof(instr_b_loop);
    // NOTE: なぜかこうしてアクセスしないと以下のオフセット計算でおかしくなるのでこうしておく
    for (int i = 0; i < 4; i++)
    {
        printf("===> %d\n", instr_b_loop[i]);
    }

    // シェルコード全体
    unsigned char shellcode[4 * 6];
    // size_t shellcode_size = sizeof(shellcode);

    int offset = 0;
    memcpy(shellcode + offset, (void*)instr_movz_x0_42, instr_movz_x0_42_size); offset += instr_movz_x0_42_size;
    printf("1--> %d\n", offset);
    memcpy(shellcode + offset, (void*)instr_mov_x16, instr_mov_x16_size);       offset += instr_mov_x16_size;
    printf("2--> %d\n", offset);
    memcpy(shellcode + offset, (void*)instr_blr_x16, instr_blr_x16_size);       offset += instr_blr_x16_size;
    printf("3--> %d\n", offset);
    memcpy(shellcode + offset, (void*)instr_b_loop, instr_b_loop_size);         offset += instr_b_loop_size;
    printf("4--> %d\n", offset);

    size_t shellcode_size = offset;
    printf("Shell code size: %d\n", offset);

    // 命令文を出力してみる
    for (int i = 0; i < 6; i++)
    {
        unsigned long result = 0;
        for (int j = 0; j < 4; j++)
        {
            int index = (i * 4) + j;
            unsigned int tmp = (unsigned int)shellcode[index];
            printf("%02x ", tmp);
            result += tmp << (8 * j);
        }
        printf("%d: %lx\n", i, result);
    }

    //----------------------------------------
    // コード領域を確保し、W→RXの順で保護を付与
    //----------------------------------------
    mach_port_t task = mach_task_self();

    mach_vm_address_t code_addr = 0;
    mach_vm_allocate(task, &code_addr, shellcode_size, VM_FLAGS_ANYWHERE);

    vm_protect(task, code_addr, shellcode_size, FALSE, VM_PROT_READ | VM_PROT_WRITE);
    mach_vm_write(task, code_addr, (vm_offset_t)shellcode, (mach_msg_type_number_t)shellcode_size);
    vm_protect(task, code_addr, shellcode_size, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);

    //----------------------------------------
    // スタック領域を確保 (1ページ)
    //----------------------------------------
    mach_vm_address_t stack_addr = 0;
    vm_size_t stack_size = 4096;
    mach_vm_allocate(task, &stack_addr, stack_size, VM_FLAGS_ANYWHERE);
    vm_protect(task, stack_addr, stack_size, FALSE, VM_PROT_READ | VM_PROT_WRITE);

    uint64_t sp = stack_addr + stack_size - 16; // 16バイトアライン

    //----------------------------------------
    // スレッド作成 & 状態設定 (ARM64)
    //----------------------------------------
    thread_act_t new_thread;
    thread_create(task, &new_thread);

    arm_thread_state64_t state;
    memset(&state, 0, sizeof(state));
    state.__pc = code_addr;   // シェルコード開始アドレス
    state.__sp = sp;          // スタックポインタ
    state.__fp = sp;          // 仮フレームポインタ

    mach_msg_type_number_t state_count = ARM_THREAD_STATE64_COUNT;
    thread_set_state(new_thread, ARM_THREAD_STATE64, (thread_state_t)&state, state_count);

    // スレッド実行開始
    thread_resume(new_thread);

    //----------------------------------------
    // メインスレッドは終了せず待機
    //----------------------------------------
    printf("[+] Shellcode thread started. It calls myfunc(42) then loops.\n");
    printf("    You can attach LLDB to check the behavior.\n");
    printf("    Press Ctrl+C to terminate.\n");

    while(1) {
        sleep(1);
    }

    return 0;
}
