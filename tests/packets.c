/* Exercise the actual packet hook without an Avidemux process or media files. */
#include "../src/safe_mp4.c"
#undef NDEBUG
#include <assert.h>

static bool next_packet(void *video,Bits *b){(void)video;(void)b;return true;}
static uint8_t prefix[]={0,0,0,2,0x67,0x80,0,0,0,2,0x68,0x80};
static uint8_t recovery[]={0,0,0,5,6,6,1,0xc4,0x80,0,0,0,2,0x41,0x88};
static uint8_t idr[]={0,0,0,2,0x65,0x88};

int main(void){
    uint8_t data[256];void *video=&data;void *old[2]={NULL,next_packet};
    Section sections[]={{0,sizeof(prefix),4,prefix},{100000,sizeof(prefix),4,prefix}};
    Proxy p={0};p.video=video;p.old_vt=old;p.sections=sections;p.count=2;active=&p;
    Bits b={0};b.data=data;b.capacity=sizeof(data);b.flags=0x10;b.pts=16666;
    memcpy(data,recovery,sizeof(recovery));b.len=sizeof(recovery);
    assert(hooked_packet(video,&b));assert(p.injections==1 && !p.failed);
    assert(b.len==sizeof(prefix)+sizeof(recovery));
    assert(!memcmp(data,prefix,sizeof(prefix)));
    assert(!memcmp(data+sizeof(prefix),recovery,sizeof(recovery)));

    /* Later recovery points need headers too, including source transitions. */
    uint8_t second[sizeof(prefix)];memcpy(second,prefix,sizeof(prefix));second[5]=0x81;
    sections[1].prefix=second;
    memcpy(data,recovery,sizeof(recovery));b.len=sizeof(recovery);b.pts=116666;
    assert(hooked_packet(video,&b));assert(p.injections==2);
    assert(!memcmp(data,second,sizeof(second)));

    /* In-band headers and ordinary non-keyframes remain byte-for-byte intact. */
    uint8_t saved[256];memcpy(saved,data,b.len);uint32_t length=b.len;
    assert(hooked_packet(video,&b));assert(p.injections==2);
    assert(b.len==length && !memcmp(data,saved,length));
    memcpy(data,recovery,sizeof(recovery));b.len=sizeof(recovery);b.flags=0;
    assert(hooked_packet(video,&b));assert(p.injections==2);
    assert(b.len==sizeof(recovery) && !memcmp(data,recovery,sizeof(recovery)));

    p.frames=0;p.injections=0;b.flags=0x10;b.pts=0;
    memcpy(data,idr,sizeof(idr));b.len=sizeof(idr);
    assert(hooked_packet(video,&b));assert(p.injections==1);

    p.frames=0;p.failed=false;b.flags=0;
    memcpy(data,recovery,sizeof(recovery));b.len=sizeof(recovery);
    assert(!hooked_packet(video,&b));assert(p.failed && strstr(p.error,"keyframe"));

    p.frames=0;p.failed=false;b.flags=0x10;data[3]=255;
    assert(!hooked_packet(video,&b));assert(p.failed && strstr(p.error,"NAL"));

    p.frames=0;p.failed=false;memcpy(data,idr,sizeof(idr));b.len=sizeof(idr);b.capacity=b.len;
    assert(!hooked_packet(video,&b));assert(p.failed && strstr(p.error,"no room"));

    p.frames=0;p.failed=false;b.capacity=sizeof(data);b.pts=UINT64_MAX;
    assert(!hooked_packet(video,&b));assert(p.failed && strstr(p.error,"timestamp"));
    puts("Packet hook regression checks passed.");return 0;
}
