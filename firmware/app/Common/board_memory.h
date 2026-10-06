#ifndef BOARD_MEMORY_H
#define BOARD_MEMORY_H
#include <stdint.h>
#define PSRAM_BYTES (8u*1024u*1024u)
#define NAND_BLOCKS 2048u
#define NAND_PAGE_BYTES 2048u
enum { MEM_OK=0, MEM_TIMEOUT=-1, MEM_ID=-2, MEM_BOUNDS=-3,
       MEM_ECC=-4, MEM_PROGRAM=-5, MEM_ERASE=-6, MEM_BAD=-7 };
typedef struct {
    uint8_t ram_id[8], nand_id[2];
    uint8_t ram_ok, nand_ok;       /* Device is present and passed validation. */
    uint8_t ram_probed, nand_probed;
    uint8_t bus_ok;                /* QSPI controller recovered after probing. */
    int last_error;
    uint32_t corrected, timeouts;
} MemoryInfo;
extern MemoryInfo board_memory;
void Memory_Init(void);
int PSRAM_Read(uint32_t address, void *data, uint32_t size);
int PSRAM_Write(uint32_t address, const void *data, uint32_t size);
int PSRAM_SelfTest(void); /* boot-only volatile RAM walking address test */
int NAND_Read(uint32_t page, uint16_t column, void *data, uint16_t size);
int NAND_Program(uint32_t page, const void *data); /* full 2048 byte page */
int NAND_Erase(uint16_t block);
int NAND_BadBlock(uint16_t block); /* 0=good, 1=bad, negative=I/O error */
#endif
