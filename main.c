#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach.h>
#include <mach/task_info.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld_images.h>

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
            break;
        }
    }

    free(local_array);

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