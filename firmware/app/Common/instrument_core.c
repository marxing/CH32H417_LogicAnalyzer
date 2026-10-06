#include "instrument_core.h"
static void push(InstrumentKeys *k, uint8_t e)
{
    uint8_t next = (k->write + 1u) & 31u;
    if(next == k->read) { ++k->dropped; return; }
    k->events[k->write] = e;
    __asm__ volatile ("" ::: "memory");
    k->write = next;
}
void Keys_Sample(InstrumentKeys *k, uint8_t mask)
{
    unsigned i;
    for(i=0; i<3; ++i) {
        uint8_t v = (mask >> i) & 1u;
        if(v != k->candidate[i]) { k->candidate[i]=v; k->debounce[i]=1; }
        else if(k->debounce[i]<3) ++k->debounce[i];
        if(k->debounce[i]==3 && k->stable[i]!=v) {
            k->stable[i]=v; k->held[i]=0;
            if(v) {
                ++k->presses[i];
                if(i<2) push(k, i==0 ? KEY_PREV : KEY_NEXT);
                else k->long_sent=0;
            } else if(i==2 && !k->long_sent) push(k, KEY_CLICK);
        }
        if(k->stable[i]) {
            if(k->held[i]<65535) ++k->held[i];
            if(i==2 && k->held[i]>=160 && !k->long_sent) {
                k->long_sent=1; push(k,KEY_BACK);
            }
            if(i<2 && k->held[i]>=100 && ((k->held[i]-100)%24)==0)
                push(k,i==0 ? KEY_PREV : KEY_NEXT);
        }
    }
}
uint8_t Keys_Pop(InstrumentKeys *k)
{
    uint8_t e;
    if(k->read==k->write) return KEY_NONE;
    e=k->events[k->read]; k->read=(k->read+1u)&31u; return e;
}
uint32_t Capture_CRC(uint32_t crc, const void *data, size_t n)
{
    const uint8_t *p=data;
    while(n--) { unsigned i; crc^=*p++; for(i=0;i<8;++i) crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u); }
    return crc;
}
int32_t Capture_Microvolts(uint16_t code, uint8_t range, uint16_t zero)
{
    static const uint32_t rf[4]={6980,28000,140000,280000};
    if(range>3 || code>1023 || zero>1023) return 0;
    /* Rin=R101+R102=99.8k; inverting AFE; nominal only until calibrated. */
    return (int32_t)(((int64_t)((int32_t)zero-code)*3300000*99800)/(1023LL*rf[range]));
}
uint32_t Capture_FindEdge(const uint16_t *p,uint32_t n,uint16_t mask,uint16_t level,uint8_t analog,uint8_t falling)
{
    uint32_t i;
    for(i=1;i<n;++i) {
        uint8_t a=analog ? p[i-1]>=level : (p[i-1]&mask)!=0;
        uint8_t b=analog ? p[i]>=level : (p[i]&mask)!=0;
        if(a!=b && b==!falling) return i;
    }
    return UINT32_MAX;
}
int Capture_HeaderValid(const CaptureHeader *h)
{
    return h->magic==CAPTURE_MAGIC && h->version==1 && h->bytes>0 &&
        h->bytes<=CAPTURE_MAX_BYTES && !(h->bytes&1) && h->mode<=1 &&
        h->channel<2 && h->range<4 && h->zero<=1023 && h->rate_hz>0 &&
        h->bits==(h->mode ? 10 : 16) &&
        (h->trigger==UINT32_MAX || h->trigger<h->bytes/2) &&
        h->header_crc==~Capture_CRC(~0u,h,offsetof(CaptureHeader,header_crc));
}
