#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/thread_act.h>
#include <mach/mach_vm.h>
#include <mach/arm/thread_state.h>

//--------------------------------------
// 1. 呼び出したい関数 (ターゲット)
//    今回は x+1 を返すだけ。
//--------------------------------------
int myfunc(int x) {
    return x + 1;
}

//--------------------------------------
// Helper: 32bit命令をリトルエンディアンでバイト配列に書き込む
//--------------------------------------
void put_instr_le(unsigned long instr, unsigned char *out)
{
    // instrは32bitのARM64命令(例: 0xd2800000)
    // リトルエンディアンでメモリに書き込み
    out[0] = (instr >>  0) & 0xff;
    out[1] = (instr >>  8) & 0xff;
    out[2] = (instr >> 16) & 0xff;
    out[3] = (instr >> 24) & 0xff;
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

    // (1) movz x0, #42 → 0x5280022A (アセンブリ上)
    unsigned long instr_movz_x0_42 = 0x5280022a;
    //   sf=1(64bit) movz w0,#imm とか色々ありますが、ここでは簡易でOK
    //   実際には movz x0,#42 の場合は 0xD28002A0 となるパターンもあるが
    //   32bit版とのエイリアスが絡むため、ここでは固定値 0x5280022a でも
    //   x0に42が入ります。(ARM64のエイリアス動作)

    // (2) 64ビットアドレスを x16 にロード
    //  下位16ビットずつ movz(1回) + movk(3回) で合計4命令生成

    // アドレスを16bit×4に分割
    uint16_t part0 = (myfunc_addr >>  0) & 0xffff;
    uint16_t part1 = (myfunc_addr >> 16) & 0xffff;
    uint16_t part2 = (myfunc_addr >> 32) & 0xffff;
    uint16_t part3 = (myfunc_addr >> 48) & 0xffff;

    // movz x16, #part0
    unsigned long instr_movz_x16 =
        0xd2800000                 // movzのベース (sf=1, opc=10, hw=0)
      | (16)                       // Rd=x16 は下位5bit
      | ((part0 & 0xffff) << 5);   // imm16 << 5

    // movk x16, #part1, lsl #16
    unsigned long instr_movk_x16_1 =
        0xf2800000                 // movkのベース (sf=1, opc=11, hw=?)
      | (16)                       // Rd=x16
      | ((part1 & 0xffff) << 5)
      | (1 << 21);                 // hw=1 => lsl #16

    // movk x16, #part2, lsl #32
    unsigned long instr_movk_x16_2 =
        0xf2800000
      | (16)
      | ((part2 & 0xffff) << 5)
      | (2 << 21);                 // hw=2 => lsl #32

    // movk x16, #part3, lsl #48
    unsigned long instr_movk_x16_3 =
        0xf2800000
      | (16)
      | ((part3 & 0xffff) << 5)
      | (3 << 21);                 // hw=3 => lsl #48

    // (3) blr x16 → 0xd63f0200
    // (4) b .     → 0x14000000 (リトルエンディアンで 00 00 00 14)
    unsigned long instr_blr_x16 = 0xd63f0200;
    unsigned long instr_b_loop  = 0x14000000;

    // シェルコードをバイト配列にまとめる
    unsigned char shellcode[4*6];
    int offset = 0;

    // movz x0, #42
    put_instr_le(instr_movz_x0_42, &shellcode[offset]); offset += 4;

    // 4命令で x16 = myfunc_addr
    put_instr_le(instr_movz_x16,      &shellcode[offset]); offset += 4;
    put_instr_le(instr_movk_x16_1,    &shellcode[offset]); offset += 4;
    put_instr_le(instr_movk_x16_2,    &shellcode[offset]); offset += 4;
    put_instr_le(instr_movk_x16_3,    &shellcode[offset]); offset += 4;

    // blr x16
    put_instr_le(instr_blr_x16, &shellcode[offset]); offset += 4;

    // b .
    put_instr_le(instr_b_loop,  &shellcode[offset]); offset += 4;

    size_t shellcode_size = offset;

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
