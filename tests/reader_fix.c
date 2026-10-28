/* Run the old and corrected load instructions on positions beyond 4 GiB. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../src/mp4_reader_fix.h"
#undef NDEBUG
#include <assert.h>

int main(int argc,char **argv){
    uint8_t *page=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(page);
    const uint8_t stub[]={0x53,0x48,0x89,0xcb,0x44,0x8b,0x43,0x08,0x4c,0x89,0xc0,0x5b,0xc3};
    memcpy(page,stub,sizeof(stub));memcpy(page+64,reader_offset_instruction,sizeof(reader_offset_instruction));
    DWORD old;assert(VirtualProtect(page,4096,PAGE_EXECUTE_READ,&old));
    assert(FlushInstructionCache(GetCurrentProcess(),page,4096));
    uint64_t atom[]={0,UINT64_C(0x101128865)};
    uint64_t (*get_position)(uint64_t*)=(void*)page;
    assert(get_position(atom)==(uint32_t)atom[1]);
    assert(reader_patch_offset(page+64));assert(page[64]==0x4c);
    assert(!memcmp(page+65,reader_offset_instruction+1,sizeof(reader_offset_instruction)-1));
    assert(reader_patch_offset(page+64)); /* Idempotent. */
    MEMORY_BASIC_INFORMATION info;assert(VirtualQuery(page,&info,sizeof(info)));
    assert(info.Protect==PAGE_EXECUTE_READ);
    assert(VirtualProtect(page,4096,PAGE_READWRITE,&old));
    memcpy(page+4,page+64,4);
    page[65]^=1;uint8_t changed[17];memcpy(changed,page+64,sizeof(changed));
    assert(VirtualProtect(page,4096,PAGE_EXECUTE_READ,&old));
    assert(FlushInstructionCache(GetCurrentProcess(),page,4096));
    assert(!reader_patch_offset(page+64));assert(!memcmp(changed,page+64,sizeof(changed)));
    uint64_t positions[]={0,UINT32_MAX,UINT64_C(0x100000000),UINT64_C(0x101128865),UINT64_C(0x20000007b)};
    for(unsigned i=0;i<sizeof(positions)/sizeof(positions[0]);i++){
        atom[1]=positions[i];assert(get_position(atom)==atom[1]);
    }
    assert(VirtualFree(page,0,MEM_RELEASE));
    if(argc==2){
        wchar_t path[32768];assert(MultiByteToWideChar(CP_UTF8,0,argv[1],-1,path,32768));
        assert(reader_file_matches(path));
    }
    wchar_t tempdir[MAX_PATH],tempfile[MAX_PATH];assert(GetTempPathW(MAX_PATH,tempdir));
    assert(GetTempFileNameW(tempdir,L"adm",0,tempfile));
    FILE *file=_wfopen(tempfile,L"wb");assert(file);fputs("unsupported reader",file);fclose(file);
    assert(!reader_file_matches(tempfile));assert(DeleteFileW(tempfile));
    puts("Reader fix checks passed: 64-bit execution, exact signature, idempotence, page protection, fingerprint refusal.");
    return 0;
}
