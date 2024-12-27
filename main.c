///
/// This code is trying to inject dylib to other process.
/// There are some resasons to fail injecting.
/// But these code are helpful to know how create shell code and how copy the code to other process.
/// 
/// I recommend to refer inject way to below gist code.
///     -> https://gist.github.com/vocaeq/fbac63d5d36bc6e1d6d99df9c92f75dc
/// This code show you that the injection works perfectly.
///

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

#define CHUNK_SIZE 256
#define MAX_PATH_LENGTH 4096

#define DEBUG_FOR_MINE 0

extern void calculate_machine_code(uintptr_t value, unsigned int register_number, unsigned char* machine_code_array);
extern void print_binary(unsigned long value);

void print_image_path(mach_port_t task, const char* imageFilePath);
boolean_t find_dyld_image(mach_port_t task, const char* imageFilePath);

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Ussage: %s <pid>\n", argv[0]);
        return 1;
    }

    // pid_t は int のエイリアス
    pid_t pid = (pid_t)atoi(argv[1]);
    kern_return_t kr;
    mach_port_t task;

#if DEBUG_FOR_MINE
    task = mach_task_self();
#else
    // ターゲットのタスクポート取得
    kr = task_for_pid(mach_task_self(), pid, &task);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "task_for_pid failed: %s\n", mach_error_string(kr));
        return 1;
    }
#endif

    // タスク情報から dyld_all_image_infos のアドレス取得
    struct task_dyld_info dyld_info;

    // mach_msg_type_number_t は unsigned int のエイリアス
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;

    // task_info_t は int のエイリアス
    // TASK_DYLD_INFO は 17 の define
    kr = task_info(task, TASK_DYLD_INFO, (task_info_t)&dyld_info, &count);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "task_info(TASK_DYLD_INFO) failed: %s\n", mach_error_string(kr));
        return 1;
    }

    mach_vm_address_t infos_addr = (mach_vm_address_t)dyld_info.all_image_info_addr;
    mach_vm_size_t infos_size = (mach_vm_size_t)dyld_info.all_image_info_size;

    if (infos_addr == 0 || infos_size == 0)
    {
        fprintf(stderr, "No dyld_all_image_infos found.\n");
        return 1;
    }

    // dyld_all_image_info 構造体を読み込み
    struct dyld_all_image_infos local_infos;
    vm_offset_t readMem = 0;
    mach_msg_type_number_t dataCnt = 0;
    kr = mach_vm_read(task, infos_addr, sizeof(local_infos), &readMem, &dataCnt);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "mach_vm_read(dyld_all_image_infos) failed: %s\n", mach_error_string(kr));
        return 1;
    }

    memcpy(&local_infos, (void*)readMem, sizeof(local_infos));
    mach_vm_deallocate(mach_task_self(), readMem, dataCnt);

    // dyld_all_image_infos のバージョンと、info array count を出力
    printf("dyld_all_image_infos.version: %u\n", local_infos.version);
    printf("dyld_all_image_infos.infoArrayCount: %u\n", local_infos.infoArrayCount);

    if (local_infos.infoArrayCount == 0 || local_infos.infoArray == NULL)
    {
        fprintf(stderr, "No images found in the target process.\n");
        return 1;
    }

    // dyld_image_info 配列を取得
    size_t arraySize = local_infos.infoArrayCount * sizeof(struct dyld_image_info);
    kr = mach_vm_read(task, (mach_vm_address_t)local_infos.infoArray, arraySize, &readMem, &dataCnt);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "mach_vm_read(infoArray) failed: %s\n", mach_error_string(kr));
        return 1;
    }

    struct dyld_image_info* local_array = (struct dyld_image_info*)malloc(arraySize);
    if (!local_array)
    {
        fprintf(stderr, "malloc failed.\n");
        mach_vm_deallocate(mach_task_self(), readMem, dataCnt);
        return 1;
    }

    memcpy(local_array, (void*)readMem, arraySize);
    mach_vm_deallocate(mach_task_self(), readMem, dataCnt);

    struct dyld_image_info* target_dyld_info;

    // ロードされたイメージ情報を表示
    for (uint32_t i = 0; i < local_infos.infoArrayCount; i++)
    {
        printf("Image %u:\n", i);
        printf("    LoadAddress: %p\n", local_array[i].imageLoadAddress);
        print_image_path(task, local_array[i].imageFilePath);
        boolean_t found = find_dyld_image(task, local_array[i].imageFilePath);
        if (found == TRUE)
        {
            printf("Found the dyld at [%s]\n", local_array[i].imageFilePath);
            target_dyld_info = (struct dyld_image_info*)&local_array[i];
            // break;
        }
    }

    if (!target_dyld_info)
    {
        free(local_array);
        return 1;
    }

    printf("\n\n--------------------------------------\n\n");
    printf("Saved dyld_image_info image file path: ");
    print_image_path(task, target_dyld_info->imageFilePath);

    // 見つけた dyld.dylib のヘッダを読み込む
    mach_vm_size_t mach_header_size = sizeof(struct mach_header_64);
    kr = mach_vm_read(task, (mach_vm_address_t)target_dyld_info->imageLoadAddress, mach_header_size, &readMem, &dataCnt);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "Failed to read dyld image hader: %s\n", mach_error_string(kr));
        free(local_array);
        mach_vm_deallocate(mach_task_self(), readMem, dataCnt);
        return 1;
    }

    struct mach_header_64* dyld_header = malloc(sizeof(struct mach_header_64));
    if (!dyld_header)
    {
        fprintf(stderr, "Failed to allocate memory for dyld_header.\n");
        free(local_array);
        mach_vm_deallocate(mach_task_self(), readMem, dataCnt);
        return 1;
    }

    memcpy(dyld_header, (void*)readMem, dataCnt);

    if (dyld_header->magic == MH_MAGIC_64)
    {
        printf("This must be dyld image!\n");
    }
    else
    {
        fprintf(stderr, "Got image is not dyld library.\n");
        goto clean_memory;
    }

    // Search __TEXT segment
    {
        // Point to the first of load commands.
        vm_address_t cur = (vm_address_t)target_dyld_info->imageLoadAddress + sizeof(struct mach_header_64);
        struct segment_command_64* linkedit = (struct segment_command_64*)malloc(sizeof(struct segment_command_64));
        struct segment_command_64* seg_cmd = (struct segment_command_64*)malloc(sizeof(struct segment_command_64));
        struct symtab_command* symtab = (struct symtab_command*)malloc(sizeof(struct symtab_command));
        struct load_command* lc = (struct load_command*)malloc(sizeof(struct load_command));
        struct nlist_64* symtab_array = NULL;

        if (!linkedit)
        {
            fprintf(stderr, "Failed to allocate memory for segment_command_64\n");
            goto clean_symbol_memory;
        }

        if (!seg_cmd)
        {
            fprintf(stderr, "Failed to allocate memory for segment_command_64\n");
            goto clean_symbol_memory;
        }

        if (!symtab)
        {
            fprintf(stderr, "Failed to allocate memory for segment_command_64\n");
            goto clean_symbol_memory;
        }

        if (!lc)
        {
            fprintf(stderr, "Failed to allocate memory for load_command\n");
            goto clean_symbol_memory;
        }

        printf("Command number: %d\n", dyld_header->ncmds);

        uint64_t slide = 0;
        mach_vm_size_t size = sizeof(struct load_command);
        for (int i = 0; i < dyld_header->ncmds; i++)
        {
            kr = mach_vm_read(task, cur, size, &readMem, &dataCnt);
            if (kr != KERN_SUCCESS)
            {
                fprintf(stderr, "Failed to read memory from target process: %s\n", mach_error_string(kr));
                goto clean_symbol_memory;
            }

            memcpy(lc, (void*)readMem, dataCnt);

            // printf("cmd: %d, size: %d\n", lc->cmd, lc->cmdsize);

            if (lc->cmd == LC_SEGMENT_64)
            {
                // Load memory as segment_command_64
                mach_vm_size_t seg_cmd_size = sizeof(struct segment_command_64);
                kr = mach_vm_read(task, cur, seg_cmd_size, &readMem, &dataCnt);
                if (kr != KERN_SUCCESS)
                {
                    fprintf(stderr, "Failed to read memory from target process for segment_command_64: %s\n", mach_error_string(kr));
                    goto clean_memory;
                }

                memcpy(seg_cmd, (void*)readMem, dataCnt);
                if (strcmp(seg_cmd->segname, SEG_TEXT) == 0)
                {
                    printf("Found text segment. [%s]\n", seg_cmd->segname);
                    printf("Text segment vmaddr: 0x%llx\n", seg_cmd->vmaddr);
                    printf("dyld loaded address: %p\n", target_dyld_info->imageLoadAddress);
                    slide = (uint64_t)target_dyld_info->imageLoadAddress - seg_cmd->vmaddr;
                    printf("Slide: %llu\n", slide);
                }
                else if (strcmp(seg_cmd->segname, SEG_LINKEDIT) == 0)
                {
                    memcpy(linkedit, seg_cmd, seg_cmd_size);
                    printf("Found link edit segment. [%s]\n", seg_cmd->segname);
                    printf("    vmaddr: 0x%llx\n", linkedit->vmaddr);
                    printf("   fileoff: %llu\n", linkedit->fileoff);
                }
            }
            else if (lc->cmd == LC_SYMTAB)
            {
                mach_vm_size_t symtab_size = sizeof(struct symtab_command);
                kr = mach_vm_read(task, cur, symtab_size, &readMem, &dataCnt);
                if (kr != KERN_SUCCESS)
                {
                    fprintf(stderr, "Failed to read memory from target process for symtab_command: %s\n", mach_error_string(kr));
                    goto clean_memory;
                }

                memcpy(symtab, (void*)readMem, dataCnt);

                printf("Symbol table info:\n");
                printf("    symoff: 0x%x\n", symtab->symoff);
                printf("    stroff: 0x%x\n", symtab->stroff);
            }

            cur += lc->cmdsize;
        }

        uintptr_t linkedit_base = slide + (linkedit->vmaddr - linkedit->fileoff);
        printf("Link edit base address: 0x%lx\n", linkedit_base);

        mach_vm_address_t symtab_addr = (mach_vm_address_t)(linkedit_base + symtab->symoff);
        mach_vm_size_t symtab_size = (mach_vm_size_t)symtab->nsyms * sizeof(struct nlist_64);
        kr = mach_vm_read(task, symtab_addr, symtab_size, &readMem, &dataCnt);
        if (kr != KERN_SUCCESS)
        {
            fprintf(stderr, "Failed to read memory from target process for symbol table: %s\n", mach_error_string(kr));
            goto clean_symbol_memory;
        }

        symtab_array = (struct nlist_64*)malloc((size_t)symtab_size);
        if (!symtab_array)
        {
            fprintf(stderr, "Failed to allocate memory for symbol table: %s\n", mach_error_string(kr));
            goto clean_symbol_memory;
        }

        memcpy(symtab_array, (void*)readMem, dataCnt);

        uintptr_t str_addr = (uintptr_t)(linkedit_base + symtab->stroff);

        kr = mach_vm_read(task, str_addr, symtab->strsize, &readMem, &dataCnt);
        if (!symtab_array)
        {
            fprintf(stderr, "Failed to allocate memory for string table: %s\n", mach_error_string(kr));
            goto clean_symbol_memory;
        }

        char* strtab = (char*)malloc(symtab->strsize);
        if (!strtab)
        {
            fprintf(stderr, "Failed to allocate memory for string table: %s\n", mach_error_string(kr));
            goto clean_symbol_memory;
        }

        memcpy(strtab, (void*)readMem, dataCnt);

        printf("Symbol numbers: %u\n", symtab->nsyms);

        struct nlist_64* dlopen_sym = NULL;
        for (uint32_t i = 0; i < symtab->nsyms; i++)
        {
            uint32_t strx = symtab_array[i].n_un.n_strx;
            const char* symname = strtab + strx;
            printf("Symbol name: %s\n", symname);

            if (symname && strcmp(symname, "_dlopen") == 0)
            {
                dlopen_sym = &symtab_array[i];
            }
        }

        if (dlopen_sym)
        {
            uint64_t dlopen_sym_addr = slide + dlopen_sym->n_value;
            printf("Found dlopen symbol!\n");
            printf("dlopen symbol address: 0x%llx\n", dlopen_sym_addr);

            void* real_dlopen = (void*)dlopen;
            printf("[DEBUG] dlsym reported dlopen: %p\n", real_dlopen);

            // シェルコードを仕込む
            // まずは、対象プロセス内にライブラリパス文字列を配置する
            const char* lib_path = "./libsample.dylib";
            size_t path_size = strlen(lib_path) + 1;

            mach_vm_address_t path_addr = 0;
            kr = mach_vm_allocate(task, &path_addr, path_size, VM_FLAGS_ANYWHERE);
            if (kr != KERN_SUCCESS)
            {
                fprintf(stderr, "Failed to allocate for lib path memory.\n");
                goto clean_symbol_memory;
            }

            kr = vm_protect(task, path_addr, path_size, FALSE, VM_PROT_READ | VM_PROT_WRITE);
            if (kr != KERN_SUCCESS)
            {
                fprintf(stderr, "Failed to change protect mode for lib path memory.\n");
                goto clean_symbol_memory;
            }

            kr = mach_vm_write(task, path_addr, (vm_offset_t)lib_path, (mach_msg_type_number_t)path_size);
            if (kr != KERN_SUCCESS)
            {
                fprintf(stderr, "Failed to write lib path to the process.\n");
                goto clean_symbol_memory;
            }

            uintptr_t path_address_value = (uintptr_t)path_addr;
            printf("Allocated path memory at %p (value: %lu)\n", (void*)path_addr, path_address_value);

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
#if DEBUG_FOR_MINE
            calculate_machine_code((uintptr_t)real_dlopen, 16, shell_code_dlopen_address);
#else
            calculate_machine_code(dlopen_sym_addr, 16, shell_code_dlopen_address);
#endif
            size_t shellcode_dlopen_address_size = sizeof(shell_code_dlopen_address);

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
                goto clean_symbol_memory;
            }

            memcpy(shell_code, shell_code_path_address, shellcode_path_address_size);
            memcpy(shell_code + shellcode_path_address_size, shell_code_rtld, shellcode_rtld_size);
            memcpy(shell_code + shellcode_path_address_size + shellcode_rtld_size, shell_code_dlopen_address, shellcode_dlopen_address_size);
            memcpy(shell_code + shellcode_path_address_size + shellcode_rtld_size + shellcode_dlopen_address_size, shell_code_blr, shellcode_blr_address_size);

            // 10 命令文出力してみる
            for (int i = 0; i < 10; i++)
            {
                unsigned long result = 0;
                for (int j = 0; j < 4; j++)
                {
                    int index = (i * 4) + j;
                    result += ((unsigned long)shell_code[index] << (8 * j));
                }
                printf("%d: %lx\n", i, result);
                // print_binary(result);
            }

            for (int i = 0; i < 40; i++)
            {
                if (i % 4 == 0)
                {
                    printf("\n");
                }

                printf("0x%x ", shell_code[i]);
            }

            printf("\n");

            // コード用メモリ領域確保
            mach_vm_address_t code_addr = 0;
            kr = mach_vm_allocate(task, &code_addr, whole_shellcode_size, VM_FLAGS_ANYWHERE);
            if (kr != KERN_SUCCESS)
            {
                fprintf(stderr, "Failed to allocate code memory on target process.\n");
                goto clean_symbol_memory;
            }
            printf("Allocated shell code memory address: %p\n", (void*)code_addr);

            // 書き込み・読み込み権限を付与
            kr = vm_protect(task, code_addr, whole_shellcode_size, FALSE, VM_PROT_READ | VM_PROT_WRITE);
            if (kr != KERN_SUCCESS)
            {
                fprintf(stderr, "Failed to change protect attribute on code memory.\n");
                goto clean_symbol_memory;
            }

            // シェルコードの書き込み
            kr = mach_vm_write(task, code_addr, (vm_offset_t)shell_code, (mach_msg_type_number_t)whole_shellcode_size);
            if (kr != KERN_SUCCESS)
            {
                fprintf(stderr, "Failed to write shell code on target process.\n");
                goto clean_symbol_memory;
            }

            // 実行権限付与に変更
            kr = vm_protect(task, code_addr, whole_shellcode_size, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
            if (kr != KERN_SUCCESS)
            {
                fprintf(stderr, "Failed to change shell code memory as executable.\n");
                goto clean_symbol_memory;
            }

            // スタック用メモリ領域確保
            mach_vm_address_t stack_addr = 0;
            vm_size_t stack_size = 4096;
            kr = mach_vm_allocate(task, &stack_addr, stack_size, VM_FLAGS_ANYWHERE);
            if (kr != KERN_SUCCESS)
            {
                fprintf(stderr, "Failed to allocate memory for stack.\n");
                goto clean_symbol_memory;
            }

            // スタック領域を読み書き可能に
            kr = vm_protect(task, stack_addr, stack_size, FALSE, VM_PROT_READ | VM_PROT_WRITE);
            if (kr != KERN_SUCCESS)
            {
                fprintf(stderr, "Failed to change memory protect for stack memory.\n");
                goto clean_symbol_memory;
            }

            // スタックポインタはスタック末尾付近
            uint64_t sp = stack_addr + stack_size - 16;

            // スレッド作成
            thread_act_t new_thread;
            kr = thread_create(task, &new_thread);
            if (kr != KERN_SUCCESS)
            {
                fprintf(stderr, "Failed to create a new thread on target process: %s\n", mach_error_string(kr));
                goto clean_symbol_memory;
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
                goto clean_symbol_memory;
            }

            // スレッド開始
            kr = thread_resume(new_thread);
            if (kr != KERN_SUCCESS)
            {
                fprintf(stderr, "Failed to resume new thread: %s\n", mach_error_string(kr));
                goto clean_symbol_memory;
            }

            printf("Type 'exit' to exit.\n");

            #define BUFFER_SIZE 100
            char input[BUFFER_SIZE];
            if (fgets(input, BUFFER_SIZE, stdin) != NULL)
            {
                input[strcspn(input, "\n")] = '\0';

                if (strcmp(input, "exit") == 0)
                {
                    printf("Exiting.\n");
                    goto clean_symbol_memory;
                }
                else
                {
                    printf("Input text: %s\n", input);
                }
            }
            else
            {
                printf("Error was happend\n");
                goto clean_symbol_memory;
            }
        }

clean_symbol_memory:
        if (seg_cmd) free(seg_cmd);
        if (linkedit) free(linkedit);
        if (strtab) free(strtab);
        if (symtab_array) free(symtab_array);
    }

clean_memory:
    if (dyld_header) free(dyld_header);
    if (local_array) free(local_array);
    mach_vm_deallocate(mach_task_self(), readMem, dataCnt);

    return 0;
}

boolean_t find_dyld_image(mach_port_t task, const char* imageFilePath)
{
    char remote_path[MAX_PATH_LENGTH];
    size_t total_read = 0;

    kern_return_t kr;

    while (total_read < MAX_PATH_LENGTH)
    {
        vm_offset_t readMem;
        mach_msg_type_number_t dataCnt;
        mach_vm_address_t read_addr = (mach_vm_address_t)((uintptr_t)imageFilePath + total_read);

        size_t to_read = CHUNK_SIZE;
        if (total_read + CHUNK_SIZE > MAX_PATH_LENGTH)
        {
            to_read = MAX_PATH_LENGTH - total_read;
        }

        kr = mach_vm_read(task, read_addr, to_read, &readMem, &dataCnt);
        if (kr != KERN_SUCCESS)
        {
            return FALSE;
        }

        memcpy(remote_path + total_read, (void*)readMem, dataCnt);

        char* null_pos = memchr(remote_path + total_read, '\0', dataCnt);
        if (null_pos)
        {
            size_t str_len = (null_pos - remote_path);
            remote_path[str_len] = '\0';
            // printf("Image file path: %s\n", remote_path);
            break;
        }

        total_read += dataCnt;
    }

    if (strstr(remote_path, "libdyld") == 0)
    {
        return FALSE;
    }
    else
    {
        return TRUE;
    }
}

void print_image_path(mach_port_t task, const char* imageFilePath)
{
    char remote_path[MAX_PATH_LENGTH];
    size_t total_read = 0;

    while (total_read < MAX_PATH_LENGTH)
    {
        vm_offset_t readMem;
        mach_msg_type_number_t dataCnt;
        mach_vm_address_t read_addr = (mach_vm_address_t)((uintptr_t)imageFilePath + total_read);

        size_t to_read = CHUNK_SIZE;
        if (total_read + CHUNK_SIZE > MAX_PATH_LENGTH)
        {
            to_read = MAX_PATH_LENGTH - total_read;
        }

        kern_return_t kr = mach_vm_read(task, read_addr, to_read, &readMem, &dataCnt);
        if (kr != KERN_SUCCESS)
        {
            break;
        }

        // readMem は mach_vm_read で確保された領域へのポインタ
        memcpy(remote_path + total_read, (void*)readMem, dataCnt);

        // Null ターミネータチェック
        char* null_pos = memchr(remote_path + total_read, '\0', dataCnt);
        if (null_pos)
        {
            // Null 終端発見
            size_t str_len = (null_pos - remote_path);
            remote_path[str_len] = '\0';
            printf("imageFilePath: %s\n", remote_path);
            break;
        }

        total_read += dataCnt;
    }
}