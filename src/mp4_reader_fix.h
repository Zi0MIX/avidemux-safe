/* In-memory correction for the exact 2.8.1 MSVC x64 fragmented MP4 reader.
 * The upstream adm_atom::getStartPos() returns uint32_t despite _atomStart
 * being int64_t. parseMoof passes this truncated position to parseTraf.
 * All downstream fragment offsets are already 64-bit. Nothing on disk is
 * patched. See compatibility/mp4-reader-fix.json for the inspected bytes. */
#include <wincrypt.h>

static const uint8_t reader_offset_instruction[]={
    0x44,0x8b,0x43,0x08,             /* mov r8d, dword ptr [rbx+8] */
    0x48,0x8d,0x54,0x24,0x20,       /* lea rdx, [rsp+0x20] */
    0x48,0x8b,0xcf,                 /* mov rcx, rdi */
    0xe8,0x84,0x8f,0xff,0xff        /* call parseTraf thunk */
};

static bool reader_file_matches(const wchar_t *path){
    static const uint8_t expected[]={
        0x90,0x94,0x5e,0xbe,0xaa,0x38,0x0c,0x11,0xd7,0xef,0xe5,0x40,0x38,0x59,0xc7,0xb3,
        0xb7,0x86,0x3c,0x10,0x58,0x43,0xb3,0x91,0x3f,0xd5,0x55,0xf2,0x71,0x0d,0xe0,0x28
    };
    FILE *file=_wfopen(path,L"rb");if(!file)return false;
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;bool ok=false;
    if(CryptAcquireContextW(&provider,NULL,NULL,PROV_RSA_AES,CRYPT_VERIFYCONTEXT) &&
       CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)){
        uint8_t buffer[16384],digest[32];size_t n;bool read_ok=true;
        while((n=fread(buffer,1,sizeof(buffer),file))!=0)
            if(!CryptHashData(hash,buffer,(DWORD)n,0)){read_ok=false;break;}
        DWORD length=sizeof(digest);
        ok=read_ok && !ferror(file) && CryptGetHashParam(hash,HP_HASHVAL,digest,&length,0) &&
            length==sizeof(digest) && !memcmp(digest,expected,sizeof(digest));
    }
    if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);
    fclose(file);return ok;
}

static bool reader_patch_offset(uint8_t *site){
    if(memcmp(site+1,reader_offset_instruction+1,sizeof(reader_offset_instruction)-1))return false;
    if(site[0]==0x4c)return true; /* Already corrected, no write needed. */
    if(site[0]!=0x44)return false;
    DWORD old;if(!VirtualProtect(site,sizeof(reader_offset_instruction),PAGE_EXECUTE_READWRITE,&old))return false;
    *(volatile uint8_t*)site=0x4c; /* REX.W: mov r8, qword ptr [rbx+8] */
    bool flushed=FlushInstructionCache(GetCurrentProcess(),site,1)!=0;
    if(!flushed){*(volatile uint8_t*)site=0x44;FlushInstructionCache(GetCurrentProcess(),site,1);}
    DWORD ignored;
    if(VirtualProtect(site,sizeof(reader_offset_instruction),old,&ignored))return flushed;
    /* Restore the original instruction if restoring protection failed. */
    *(volatile uint8_t*)site=0x44;FlushInstructionCache(GetCurrentProcess(),site,1);
    VirtualProtect(site,sizeof(reader_offset_instruction),old,&ignored);return false;
}

static bool fix_fragmented_mp4_positions(void){
    HMODULE reader=GetModuleHandleW(L"libADM_dm_mp4.dll");
    if(!reader){fprintf(stderr,"[SafeMP4] MP4 64-bit fragment fix NOT applied: reader not loaded.\n");return false;}
    uint8_t *base=(uint8_t*)reader;
    IMAGE_DOS_HEADER *dos=(void*)base;
    IMAGE_NT_HEADERS64 *nt=(void*)(base+dos->e_lfanew);
    wchar_t path[32768];DWORD length=GetModuleFileNameW(reader,path,32768);
    if(nt->FileHeader.TimeDateStamp!=1663416777u || nt->OptionalHeader.SizeOfImage!=143360u ||
       !length || length>=32768 || !reader_file_matches(path)){
        fprintf(stderr,"[SafeMP4] MP4 64-bit fragment fix NOT applied: unsupported reader fingerprint.\n");return false;
    }
    /* Confirm parseTraf still receives and saves a qword argument. */
    static const uint8_t callee[]={0x4c,0x89,0x44,0x24,0x18};
    if(memcmp(base+0x8530,callee,sizeof(callee)) || !reader_patch_offset(base+0x84c6)){
        fprintf(stderr,"[SafeMP4] MP4 64-bit fragment fix NOT applied: instruction or memory protection mismatch.\n");return false;
    }
    printf("[SafeMP4] MP4 64-bit fragment positions enabled (reader RVA 0x84c6).\n");return true;
}
