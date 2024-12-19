#include <stdio.h>
#include <stdint.h>

int main() {
    uint64_t remote_path_addr = 0x1234567890abcdef; // 例: ダミーアドレス
    uint64_t dlopen_addr = 0xfedcba0987654321; // 例: ダミーアドレス

    asm (
        "mov rdi, %0\n"
        "mov rsi, 2\n"
        "mov rax, %1\n"
        "call rax\n"
        "ret\n"
        : // 出力オペランド (この場合はなし)
        : "r"(remote_path_addr), "r"(dlopen_addr) // 入力オペランド
        : "rdi", "rsi", "rax" // 破壊されるレジスタ
    );

    printf("Returned from assembly code.\n");

    return 0;
}