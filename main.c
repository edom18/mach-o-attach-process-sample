#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach.h>
#include <mach/task_info.h>
#include <mach/mach_vm.h>
#include <mach-o/loader.h>
#include <mach-o/dyld_images.h>
#include <mach-o/nlist.h>

#define CHUNK_SIZE 256
#define MAX_PATH_LENGTH 4096

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

    // ターゲットのタスクポート取得
    kr = task_for_pid(mach_task_self(), pid, &task);
    if (kr != KERN_SUCCESS)
    {
        fprintf(stderr, "task_for_pid failed: %s\n", mach_error_string(kr));
        return 1;
    }

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

    struct dyld_image_info* local_array = malloc(arraySize);
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
        // printf("Image %u:\n", i);
        // printf("    LoadAddress: %p\n", local_array[i].imageLoadAddress);
        // print_image_path(task, local_array[i].imageFilePath);
        boolean_t found = find_dyld_image(task, local_array[i].imageFilePath);
        if (found == TRUE)
        {
            printf("Found the dyld at [%s]\n", local_array[i].imageFilePath);
            target_dyld_info = (struct dyld_image_info*)&local_array[i];
            break;
        }
    }

    if (!target_dyld_info)
    {
        free(local_array);
        return 1;
    }

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

    if (dyld_header->magic != MH_MAGIC_64)
    {
        fprintf(stderr, "Got image is not dyld library.\n");
        goto clean_memory;
    }

    printf("This must be dyld image!\n");

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
            return 1;
        }

        if (!seg_cmd)
        {
            fprintf(stderr, "Failed to allocate memory for segment_command_64\n");
            goto clean_symbol_memory;
            return 1;
        }

        if (!symtab)
        {
            fprintf(stderr, "Failed to allocate memory for segment_command_64\n");
            goto clean_symbol_memory;
            return 1;
        }

        if (!lc)
        {
            fprintf(stderr, "Failed to allocate memory for load_command\n");
            goto clean_symbol_memory;
            return 1;
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
                return 1;
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
                    return 1;
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
                    printf("Found link edit segment\n");
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
                    return 1;
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
            return 1;
        }

        symtab_array = (struct nlist_64*)malloc((size_t)symtab_size);
        if (!symtab_array)
        {
            fprintf(stderr, "Failed to allocate memory for symbol table: %s\n", mach_error_string(kr));
            goto clean_symbol_memory;
            return 1;
        }

        memcpy(symtab_array, (void*)readMem, dataCnt);

        uintptr_t str_addr = (uintptr_t)(linkedit_base + symtab->stroff);

        kr = mach_vm_read(task, str_addr, symtab->strsize, &readMem, &dataCnt);
        if (!symtab_array)
        {
            fprintf(stderr, "Failed to allocate memory for string table: %s\n", mach_error_string(kr));
            goto clean_symbol_memory;
            return 1;
        }

        char* strtab = (char*)malloc(symtab->strsize);
        if (!strtab)
        {
            fprintf(stderr, "Failed to allocate memory for string table: %s\n", mach_error_string(kr));
            goto clean_symbol_memory;
            return 1;
        }

        memcpy(strtab, (void*)readMem, dataCnt);

        printf("Symbol numbers: %u\n", symtab->nsyms);

        struct nlist_64* dlopen_sym = NULL;
        for (uint32_t i = 0; i < symtab->nsyms; i++)
        {
            uint32_t strx = symtab_array[i].n_un.n_strx;
            const char* symname = strtab + strx;
            // printf("Symbol name: %s\n", symname);

            if (symname && strcmp(symname, "_dlopen") == 0)
            {
                dlopen_sym = &symtab_array[i];
            }
        }

        if (dlopen_sym)
        {
            printf("Found dlopen symbol!\n");
            printf("dlopen symbol address: 0x%llx\n", dlopen_sym->n_value);
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