#ifndef MW_COMMAND_GUARD_H
#define MW_COMMAND_GUARD_H
#include <stdint.h>

/* L4: legacy protocol payload lengths, independent of MCU/HAL.
 * Sources: logic dispatcher and host ch32h417_send_command call sites.
 * This checks the DECLARED length only. Transport must separately validate
 * actual RX bytes; a truncated packet with a forged valid length is not caught.
 * Unknown opcodes retain the dispatcher's existing unsupported-command reply.
 */
static inline int mw_command_length_valid(uint8_t cmd, uint8_t length)
{
    switch(cmd) {
    case 0xa0: case 0xa1: case 0xa6: case 0xaf:
        return length == 2u;
    case 0xa7:
        return length == 1u;
    case 0xa2: case 0xa3: case 0xa4: case 0xa5:
    case 0xa9: case 0xaa: case 0xab:
    case 0xac: case 0xae: case 0xb0: case 0xb1: case 0xc0: case 0xc3:
        return length == 0u;
    case 0xc4:
        return length == 1u;
    case 0xa8:
        return length == 0u || length == 4u;
    case 0xc1: case 0xc2:
        return length == 4u;
    default:
        return 1;
    }
}
#endif
