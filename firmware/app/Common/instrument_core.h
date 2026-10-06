#ifndef INSTRUMENT_CORE_H
#define INSTRUMENT_CORE_H
#include <stdint.h>
#include <stddef.h>
enum { KEY_NONE, KEY_PREV, KEY_NEXT, KEY_CLICK, KEY_BACK };
typedef struct {
    uint8_t candidate[3], stable[3], debounce[3], long_sent;
    uint16_t held[3];
    volatile uint8_t read, write;
    uint8_t events[32];
    volatile uint32_t dropped, presses[3];
} InstrumentKeys;
void Keys_Sample(InstrumentKeys *k, uint8_t active_mask); /* exactly 5 ms */
uint8_t Keys_Pop(InstrumentKeys *k);
uint32_t Capture_CRC(uint32_t crc, const void *data, size_t size);
int32_t Capture_Microvolts(uint16_t code, uint8_t range, uint16_t zero);
uint32_t Capture_FindEdge(const uint16_t *data, uint32_t n, uint16_t mask,
                         uint16_t threshold, uint8_t analog, uint8_t falling);
#define CAPTURE_MAGIC 0x31414c44u
#define CAPTURE_MAX_BYTES 65536u
/* Fixed little-endian file format, not the legacy USB/library ABI. */
typedef struct {
    uint32_t magic, version, sequence, bytes, rate_hz, trigger, data_crc;
    uint8_t mode, channel, range, bits;
    uint16_t zero, flags;
    uint32_t reserved[6], header_crc;
} CaptureHeader;
int Capture_HeaderValid(const CaptureHeader *h);
#endif
