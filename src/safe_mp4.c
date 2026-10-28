/* Version-locked experimental adapter for Avidemux 2.8.1 MSVC x64.
 * C ABI bridge to the existing muxer. No encoding or second media pass.
 * Tested in a separate portable copy, never install over an existing DLL.
 * Virtual slots follow upstream 2.8.1 ADM_muxer.h and ADM_videoStream.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "mp4_reader_fix.h"

#define EXPORT __declspec(dllexport)
typedef struct {uint32_t len,capacity;uint8_t *data;uint32_t flags,iq,oq;uint64_t pts,dts;} Bits;
typedef struct {uint64_t start;uint32_t length,width;uint8_t *prefix;} Section;
typedef struct Proxy {void **vt;void *real;void *video;void **old_vt;void *hook_vt[32];
    Section *sections;uint32_t count;uint64_t origin,frames,injections;bool failed;const char *error;char *filename;} Proxy;
static HMODULE original;
static Proxy *active;
static void *proxy_vtable[13];
static void protect_legacy_debug_dump(void);
static void **vtable(void *p){return *(void ***)p;}
static FARPROC symbol(const char *name){return GetProcAddress(original,name);}
static void dirpath(wchar_t *path,const wchar_t *tail){
    GetModuleFileNameW(NULL,path,32768);wchar_t *p=wcsrchr(path,L'\\');if(p)p[1]=0;wcscat(path,tail);
}
static bool load_original(void){
    if(original)return true;wchar_t path[32768];dirpath(path,L"plugins\\muxers\\libADM_mx_mp4.dll");
    original=LoadLibraryW(path);if(original){fix_fragmented_mp4_positions();protect_legacy_debug_dump();}return original!=NULL;
}
/* 2.8.1's MP4 reader assumes one SPS when printing avcC. A duplicate SPS
 * makes its diagnostic mixDump length run outside the allocation. Suppress
 * only this reader's hex-dump import; packet parsing/data remain untouched. */
static void safe_debug_dump(const uint8_t *data,int length){(void)data;(void)length;}
static void protect_legacy_debug_dump(void){
    uint8_t *base=(uint8_t*)GetModuleHandleW(L"libADM_dm_mp4.dll");if(!base)return;
    IMAGE_DOS_HEADER *dos=(void*)base;IMAGE_NT_HEADERS64 *nt=(void*)(base+dos->e_lfanew);
    DWORD rva=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if(!rva)return;IMAGE_IMPORT_DESCRIPTOR *d=(void*)(base+rva);
    for(;d->Name;d++){
        if(!d->OriginalFirstThunk)continue;
        IMAGE_THUNK_DATA64 *names=(void*)(base+d->OriginalFirstThunk),*slots=(void*)(base+d->FirstThunk);
        for(;names->u1.AddressOfData;names++,slots++){
            if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))continue;
            IMAGE_IMPORT_BY_NAME *name=(void*)(base+names->u1.AddressOfData);
            if(strstr((char*)name->Name,"mixDump")){
                DWORD old;if(VirtualProtect(&slots->u1.Function,sizeof(void*),PAGE_READWRITE,&old)){
                    InterlockedExchangePointer((void *volatile*)&slots->u1.Function,(void*)safe_debug_dump);
                    DWORD ignored;VirtualProtect(&slots->u1.Function,sizeof(void*),old,&ignored);
                    HMODULE pin;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                        (const wchar_t*)(uintptr_t)safe_debug_dump,&pin);
                }
            }
        }
    }
}
static FILE *utf8open(const char *name,const wchar_t *mode){
    int n=MultiByteToWideChar(CP_UTF8,0,name,-1,NULL,0);if(!n)return NULL;
    wchar_t *w=malloc(n*sizeof(wchar_t));MultiByteToWideChar(CP_UTF8,0,name,-1,w,n);
    FILE *f=_wfopen(w,mode);free(w);return f;
}
static void restore(Proxy *p){if(p->video && p->old_vt){*(void ***)p->video=p->old_vt;p->old_vt=NULL;}if(active==p)active=NULL;}
static uint32_t be32(const uint8_t *b){return (uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3];}
static bool headers_from_avcc(Section *s,const uint8_t *c,uint32_t length){
    if(length<7 || c[0]!=1 || (c[4]&3)!=3)return false;
    s->prefix=malloc(4096);s->width=4;s->length=0;uint32_t pos=6;
    for(unsigned group=0;group<2;group++){
        if(pos>=length)return false;uint32_t count=group?c[pos++]:c[5]&31;
        for(uint32_t i=0;i<count;i++){
            if(pos+2>length)return false;uint32_t n=(uint32_t)c[pos]<<8|c[pos+1];pos+=2;
            if(!n || n>length-pos)return false;
            uint32_t size=n;while(size && !c[pos+size-1])size--;
            bool found=false;for(uint32_t at=0;at<s->length;){uint32_t old=be32(s->prefix+at);at+=4;
                if(old==size && !memcmp(s->prefix+at,c+pos,size))found=true;at+=old;}
            if(!found){if(!size || size+4>4096-s->length)return false;uint8_t *to=s->prefix+s->length;
                to[0]=size>>24;to[1]=size>>16;to[2]=size>>8;to[3]=size;memcpy(to+4,c+pos,size);s->length+=size+4;}
            pos+=n;
        }
    }return s->length>0;
}
/* The inspected MSVC build's getExtraData wrapper loads video_body with a
 * RIP-relative mov. Resolve that data address within the fingerprinted EXE;
 * all subsequent calls follow the public upstream 2.8.1 IEditor interface.
 * This removes scripts, plan files and source I/O from normal exports. */
static bool readable(void *address,size_t size){MEMORY_BASIC_INFORMATION m;
    return VirtualQuery(address,&m,sizeof(m))==sizeof(m) && m.State==MEM_COMMIT &&
        !(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)) && (uint8_t*)address+size<=(uint8_t*)m.BaseAddress+m.RegionSize;}
static void *live_editor(void *video){
    uint8_t *base=(uint8_t*)GetModuleHandleW(NULL);IMAGE_DOS_HEADER *dos=(void*)base;
    IMAGE_NT_HEADERS64 *nt=(void*)(base+dos->e_lfanew);uint8_t *end=base+nt->OptionalHeader.SizeOfImage;
    if(!((nt->FileHeader.TimeDateStamp==1663416683u && nt->OptionalHeader.SizeOfImage==3657728u) ||
         (nt->FileHeader.TimeDateStamp==1663417445u && nt->OptionalHeader.SizeOfImage==2555904u)))return NULL;
    uint8_t *fn=vtable(video)[2];
    for(int j=0;j<2 && fn[0]==0xe9;j++){int32_t delta;memcpy(&delta,fn+1,4);fn+=5+delta;}
    if(fn<base || fn+64>end)return NULL;
    for(int i=0;i<48;i++)if(fn[i]==0x48 && fn[i+1]==0x8b && fn[i+2]==0x0d){
        int32_t delta;memcpy(&delta,fn+i+3,4);uint8_t *global=fn+i+7+delta;
        if(global<base || global+8>end)continue;void *editor=*(void**)global;
        if(!readable(editor,8))continue;void **vt=vtable(editor);
        if((uint8_t*)vt<base || (uint8_t*)(vt+31)>end)continue;
        if((uint8_t*)vt[24]>=base && (uint8_t*)vt[24]<end)return editor;
    }
    printf("[SafeMP4] Could not resolve inspected editor wrapper:");for(int i=0;i<24;i++)printf(" %02x",fn[i]);printf("\n");return NULL;
}
typedef struct {uint32_t reference;uint64_t ref_start,time,duration;} NativeSegment;
static bool live_plan(Proxy *p,void *video){
    void *editor=live_editor(video);if(!editor)return false;void **vt=vtable(editor);
    uint64_t a=((uint64_t(*)(void*))vt[15])(editor),b=((uint64_t(*)(void*))vt[16])(editor);
    uint32_t count=((uint32_t(*)(void*))vt[17])(editor);
    if(!count || count>100000 || b<=a)return false;p->sections=calloc(count,sizeof(Section));p->count=0;
    for(uint32_t i=0;i<count;i++){
        NativeSegment *segment=((void*(*)(void*,int))vt[24])(editor,i);
        if(!readable(segment,sizeof(NativeSegment)) || segment->duration>UINT64_MAX-segment->time)return false;
        uint64_t first=segment->time>a?segment->time:a,last=segment->time+segment->duration;
        if(last>b)last=b;if(first>=last)continue;
        void *reference=((void*(*)(void*,int))vt[30])(editor,segment->reference);
        if(!readable(reference,8))return false;void *demux=*(void**)reference;
        if(!readable(demux,8))return false;uint32_t length=0;uint8_t *extra=NULL;
        if(!((uint8_t(*)(void*,uint32_t*,uint8_t**))vtable(demux)[2])(demux,&length,&extra) || length>4096 || !readable(extra,length))return false;
        Section *s=p->sections+p->count++;s->start=first-a;
        if(!headers_from_avcc(s,extra,length))return false;
    }
    return p->count && !p->sections[0].start;
}
static bool packet_error(Proxy *p,const char *error,const Bits *b){
    p->failed=true;p->error=error;
    fprintf(stderr,"[SafeMP4] ERROR: %s (packet=%llu pts=%llu dts=%llu flags=0x%x bytes=%u capacity=%u)\n",
        error,(unsigned long long)p->frames,(unsigned long long)b->pts,(unsigned long long)b->dts,b->flags,b->len,b->capacity);
    return false;
}
static void report_error(Proxy *p,const char *fallback){
    const char *error=p->error?p->error:fallback;
    fprintf(stderr,"[SafeMP4] ERROR: %s\n",error);
    /* CLI automation must never wait for a dialog. The GUI otherwise only
     * reports a generic save failure, hiding the packet rejection reason. */
    wchar_t exe[32768];GetModuleFileNameW(NULL,exe,32768);
    if(!wcsstr(exe,L"avidemux_cli") && !wcsstr(GetCommandLineW(),L"--nogui"))
        MessageBoxA(NULL,error,"MP4 with source headers: save failed",MB_OK|MB_ICONERROR|MB_TASKMODAL);
}
static bool hooked_packet(void *video,Bits *b){
    Proxy *p=active;if(!p || p->video!=video)return false;
    bool ok=((bool(*)(void*,Bits*))p->old_vt[1])(video,b);if(!ok)return false;
    bool first=p->frames++==0;
    if(first){p->origin=b->pts;if(b->pts==UINT64_MAX)return packet_error(p,"The first video packet has no presentation timestamp.",b);}
    if(!first && !(b->flags&0x10))return true;
    uint32_t pos=0;uint8_t picture=0;
    if(b->len>b->capacity)return packet_error(p,"The video packet exceeds its buffer capacity.",b);
    while(pos+4<=b->len){uint32_t size=be32(b->data+pos);pos+=4;
        if(!size || size>b->len-pos)return packet_error(p,"Invalid length-prefixed H.264 NAL unit.",b);
        uint8_t type=b->data[pos]&31;
        if(type>=1 && type<=5){picture=type;break;}pos+=size;
    }
    /* Recovery-point I pictures use non-IDR slices. Avidemux validates Copy
     * cuts and marks these as keyframes too. Insert headers before all such
     * pictures, including the first packet; do not impose an IDR-only cut. */
    if(first && (!picture || (picture!=5 && !(b->flags&0x10))))
        return packet_error(p,"The selection does not start on a video keyframe. Move marker A to a keyframe.",b);
    if(!picture)return true;
    if(b->pts==UINT64_MAX || b->pts<p->origin)return packet_error(p,"A video keyframe has an invalid presentation timestamp.",b);
    uint64_t time=b->pts-p->origin;uint32_t lo=0,hi=p->count;
    while(lo+1<hi){uint32_t mid=lo+(hi-lo)/2;if(p->sections[mid].start<=time+2)lo=mid;else hi=mid;}
    Section *s=p->sections+lo;
    /* Twitch already carries the right SPS/PPS. Keep those packets verbatim. */
    for(uint32_t at=0;at+4<=b->len;){
        uint32_t size=be32(b->data+at);if(!size || size>b->len-at-4)break;
        uint8_t type=b->data[at+4]&31;if(type>=1 && type<=5)break;
        if(s->length<=b->len-at && !memcmp(b->data+at,s->prefix,s->length))return true;
        at+=size+4;
    }
    if(s->length>b->capacity-b->len)return packet_error(p,"The video packet buffer has no room for the source SPS/PPS headers.",b);
    memmove(b->data+s->length,b->data,b->len);memcpy(b->data,s->prefix,s->length);b->len+=s->length;p->injections++;
    return true;
}

/* Patch only the four-byte codec tag after native muxer close. All packets
 * were already written with in-band SPS/PPS. No index movement or scan. */
static bool patch_boxes(FILE *f,uint64_t start,uint64_t end,unsigned depth){
    if(depth>8)return false;
    uint8_t head[16];uint64_t pos=start;
    while(pos+8<=end){_fseeki64(f,pos,SEEK_SET);if(fread(head,1,8,f)!=8)return false;
        uint64_t size=be32(head);uint32_t h=8;
        if(size==1){if(fread(head+8,1,8,f)!=8)return false;size=(uint64_t)be32(head+8)<<32|be32(head+12);h=16;}
        if(size==0)size=end-pos;if(size<h || size>end-pos)return false;
        uint32_t type=be32(head+4);
        if(type==0x73747364 && size>=h+16){
            uint8_t data[16];_fseeki64(f,pos+h,SEEK_SET);if(fread(data,1,16,f)!=16)return false;
            if(be32(data+4)!=1)return false;
            if(!memcmp(data+12,"avc1",4) || !memcmp(data+12,"avc3",4)){
                _fseeki64(f,pos+h+12,SEEK_SET);return fwrite("avc3",1,4,f)==4;
            }
        }else if(type==0x6d6f6f76 || type==0x7472616b || type==0x6d646961 || type==0x6d696e66 || type==0x7374626c){
            if(patch_boxes(f,pos+h,pos+size,depth+1))return true;
        }
        pos+=size;
    }return false;
}
static bool patch_tag(const char *name){FILE *f=utf8open(name,L"r+b");if(!f)return false;
    _fseeki64(f,0,SEEK_END);uint64_t end=_ftelli64(f);bool ok=patch_boxes(f,0,end,0);fclose(f);return ok;}
static bool proxy_open(Proxy *p,const char *name,void *video,uint32_t count,void **audio){
    if(active || !live_plan(p,video)){report_error(p,"Could not read the source-header plan from this Avidemux build or selection.");return false;}
    uint32_t fourcc=*(uint32_t*)((uint8_t*)video+28);
    if(fourcc!=0x34363248 && fourcc!=0x31435641){report_error(p,"MP4 with source headers requires H.264 video in Copy mode.");return false;}
    uint32_t length=0;uint8_t *extra=NULL;
    if(!((bool(*)(void*,uint32_t*,uint8_t**))vtable(video)[2])(video,&length,&extra) || length<7 || extra[0]!=1 || (extra[4]&3)!=3){report_error(p,"The video needs AVC headers with four-byte NAL lengths.");return false;}
    p->filename=_strdup(name);p->video=video;p->old_vt=vtable(video);
    memcpy(p->hook_vt,p->old_vt,sizeof(p->hook_vt));p->hook_vt[1]=(void*)hooked_packet;
    active=p;*(void ***)video=p->hook_vt;
    bool ok=((bool(*)(void*,const char*,void*,uint32_t,void**))vtable(p->real)[1])(p->real,name,video,count,audio);
    if(!ok){restore(p);report_error(p,"The native MP4 muxer could not open the output file. See the Avidemux log.");}return ok;
}
static bool proxy_save(Proxy *p){bool ok=((bool(*)(void*))vtable(p->real)[2])(p->real);
    restore(p);printf("[SafeMP4] packets=%llu injected_keyframes=%llu failed=%u\n",(unsigned long long)p->frames,(unsigned long long)p->injections,p->failed);
    if(p->failed)report_error(p,"Could not prepare a video packet for MP4.");
    return ok && !p->failed;}
static bool proxy_close(Proxy *p){restore(p);bool ok=((bool(*)(void*))vtable(p->real)[3])(p->real);
    if(p->filename && !patch_tag(p->filename)){if(!p->failed)report_error(p,"Could not set the output MP4 video tag to avc3. Check file access and the Avidemux log.");p->failed=true;ok=false;}return ok && !p->failed;}
static void *proxy_encoding(Proxy *p){return ((void*(*)(void*))vtable(p->real)[4])(p->real);}
static void proxy_name(Proxy *p,const char *s){((void(*)(void*,const char*))vtable(p->real)[5])(p->real,s);}
static bool proxy_init(Proxy *p,const char *s){return ((bool(*)(void*,const char*))vtable(p->real)[6])(p->real,s);}
static bool proxy_ui(Proxy *p,uint64_t t){return ((bool(*)(void*,uint64_t))vtable(p->real)[7])(p->real,t);}
static bool proxy_update(Proxy *p){return ((bool(*)(void*))vtable(p->real)[8])(p->real);}
static bool proxy_closeui(Proxy *p){return ((bool(*)(void*))vtable(p->real)[9])(p->real);}
static bool proxy_global(Proxy *p){return ((bool(*)(void*))vtable(p->real)[10])(p->real);}
static bool proxy_annex(Proxy *p){return ((bool(*)(void*))vtable(p->real)[11])(p->real);}
static bool proxy_ts(Proxy *p){return ((bool(*)(void*))vtable(p->real)[12])(p->real);}
EXPORT void destroy(void *object){Proxy *p=object;if(!p)return;restore(p);
    ((void(*)(void*))symbol("destroy"))(p->real);
    for(uint32_t i=0;i<p->count;i++)free(p->sections[i].prefix);free(p->sections);free(p->filename);free(p);}
static void *proxy_dtor(Proxy *p,unsigned flags){destroy(p);return NULL;}
EXPORT void *create(void){if(!load_original())return NULL;Proxy *p=calloc(1,sizeof(Proxy));
    void *methods[]={proxy_dtor,proxy_open,proxy_save,proxy_close,proxy_encoding,proxy_name,proxy_init,proxy_ui,proxy_update,proxy_closeui,proxy_global,proxy_annex,proxy_ts};
    memcpy(proxy_vtable,methods,sizeof(methods));p->vt=proxy_vtable;p->real=((void*(*)(void))symbol("create"))();return p;}
EXPORT const char *getName(void){return "SafeMP4";}
EXPORT const char *getDisplayName(void){return "MP4 with source headers (POC)";}
EXPORT const char *getDescriptor(void){return "H.264 source-header injector, Avidemux 2.8.1 MSVC x64 POC";}
EXPORT const char *getDefaultExtension(void){return "mp4";}
EXPORT uint32_t getApiVersion(void){return load_original()?((uint32_t(*)(void))symbol("getApiVersion"))():9;}
EXPORT uint8_t getVersion(uint32_t *a,uint32_t *b,uint32_t *c){*a=0;*b=1;*c=2;return 1;}
EXPORT bool configure(void){return load_original() && ((bool(*)(void))symbol("configure"))();}
EXPORT bool getConfiguration(void **p){return load_original() && ((bool(*)(void**))symbol("getConfiguration"))(p);}
EXPORT bool setConfiguration(void *p){return load_original() && ((bool(*)(void*))symbol("setConfiguration"))(p);}
EXPORT bool resetConfiguration(void){return load_original() && ((bool(*)(void))symbol("resetConfiguration"))();}
EXPORT void clearDefaultConfig(void){/* Native plugin owns its shared defaults. */}
