#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

// Mach-O パース用ヘルパー：特定のセグメントコマンドを探す
static struct load_command* find_load_command(const struct mach_header_64* header, uint32_t cmd)
{
    struct load_command* lc = (struct load_command*)((uintptr_t)header + sizeof(struct mach_header_64));
    for (uint32_t i = 0; i < header->ncmds; i++)
    {
        if (lc->cmd == cmd)
        {
            return lc;
        }
        lc = (struct load_command*)((uintptr_t)lc + lc->cmdsize);
    }
    return NULL;
}

// 文字列を安全に取り出す（シンボルテーブル参照用）
static const char* safe_str(const char* base, uint32_t offset, uint32_t strsize)
{
    if (offset < strsize)
    {
        return base + offset;
    }

    return NULL;
}

// _dyld_image_count などは、他プロセスに対しては使えないため、
// 本来であれば task_for_pid() でタスクハンドルを取得し、
// task_info() で取得する dyld_all_image_infos 構造体から検索する必要があるよう
int main(void)
{
    // 1. 全イメージ数取得
    uint32_t image_count = _dyld_image_count();

    printf("Image count: %d\n", image_count);

    // 2. libdyld.dylib を探す
    const struct mach_header_64* libdyld_header = NULL;
    intptr_t slide = 0;
    for (uint32_t i = 0; i < image_count; i++)
    {
        const char* name = _dyld_get_image_name(i);
        // strstr は、第一引数の文字列が、第二引数の文字列のどこに出現するかのインデックスを返してくれる関数
        if (name && strstr(name, "libdyld.dylib") != NULL)
        {
            libdyld_header = (const struct mach_header_64*)_dyld_get_image_header(i);
            slide = _dyld_get_image_vmaddr_slide(i);
            printf("Found libdylib.dylib at %p with slide 0x%lx\n", (void*)libdyld_header, (unsigned long)slide);
            break;
        }
    }

    if (!libdyld_header)
    {
        fprintf(stderr, "libdyld.dylib not found.\n");
        return 1;
    }

    // 3. Mach-O ヘッダから LC_SYMTAB と __LINKEDIT セグメント情報を取得
    struct symtab_command* symtab = NULL;
    struct segment_command_64* linkedit = NULL;

    {
        struct load_command* lc = (struct load_command*)((uintptr_t)libdyld_header + sizeof(struct mach_header_64));
        for (uint32_t i = 0; i < libdyld_header->ncmds; i++)
        {
            if (lc->cmd == LC_SYMTAB)
            {
                symtab = (struct symtab_command*)lc;
            }
            else if (lc->cmd == LC_SEGMENT_64)
            {
                struct segment_command_64* seg = (struct segment_command_64*)lc;
                if (strcmp(seg->segname, SEG_LINKEDIT) == 0)
                {
                    linkedit = seg;
                }
            }
            lc = (struct load_command*)((uintptr_t)lc + lc->cmdsize);
        }
    }

    if (!symtab || !linkedit)
    {
        fprintf(stderr, "Failed to find LC_SYMTAB or __LINKEDIT.\n");
        return 1;
    }

    printf("LC_SYMTAB and __LINKEDIT load commands are found.\n");

    // 4. シンボルテーブル、ストリングテーブルへの実行時アドレス計算
    uintptr_t linkedit_base = slide + (linkedit->vmaddr - linkedit->fileoff);

    printf("__LINKEDIT load command vmaddr: 0x%llx\n", linkedit->vmaddr);

    // 注意: 実際には linkedit->vmaddr はロード時アドレスを表すので、
    // slide 適用は (linkedit->vmaddr + slide) が実行時アドレス基点になる
    // ここでは参考程度
    // 正確には:
    // 実行時 __LINKEDIT ベース = base + slide + (linkedit->fileoff - file_starting_offset)
    // ただし今回は libdyld.dylib が標準的な配置であると仮定

    // シンボルテーブル
    struct nlist_64* symtab_array = (struct nlist_64*)(linkedit_base + symtab->symoff);
    const char* strtab_base = (const char*)(linkedit_base + symtab->stroff);

    // ストリングテーブルのサイズは symtab->strsize
    // シンボル数は symtab->nsyms

    // 5. `_dlopen` シンボルを検索
    struct nlist_64* dlopen_sym = NULL;
    for (uint32_t i = 0; i < symtab->nsyms; i++)
    {
        uint32_t strx = symtab_array[i].n_un.n_strx;
        const char* symname = safe_str(strtab_base, strx, symtab->strsize);
        if (symname && strcmp(symname, "_dlopen") == 0)
        {
            dlopen_sym = &symtab_array[i];
            break;
        }
    }

    if (!dlopen_sym)
    {
        fprintf(stderr, "_dlopen symbol not found.\n");
        return 1;
    }

    // 6. dlopen アドレス計算: n_value + slide
    uintptr_t dlopen_addr = dlopen_sym->n_value + slide;

    printf("Calculated dlopen address: %p\n", (void*)dlopen_addr);

    // 検証として、実際に dlsym から取得したアドレスと比較
    void* real_dlopen = dlsym(RTLD_DEFAULT, "dlopen");
    printf("dlsym reported dlopen: %p\n", real_dlopen);

    return 0;
}