#include <assert.h>
#include <stdio.h>
#include "mw_command_guard.h"
int main(void)
{
    const unsigned char commands[] = {
        0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,
        0xaa,0xab,0xac,0xae,0xaf,0xb0,0xb1,0xc0,0xc1,0xc2,0xc3,0xc4
    };
    const unsigned char lengths[] = {
        2,2,0,0,0,0,2,1,0,0,0,0,0,0,2,0,0,0,4,4,0,1
    };
    for(unsigned cmd=0;cmd<256;++cmd) {
        int expected=-1;
        for(unsigned i=0;i<sizeof commands;++i)
            if(cmd==commands[i]) expected=lengths[i];
        for(unsigned n=0;n<256;++n)
            assert(mw_command_length_valid((uint8_t)cmd,(uint8_t)n)==
                   (expected<0 || n==(unsigned)expected || (cmd==0xa8u && n==4u)));
    }
    puts("PASS: 65536 command/declared-length combinations");
    return 0;
}
