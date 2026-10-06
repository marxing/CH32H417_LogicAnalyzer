"""Extract and execute the actual production exporter with mock state, no USB."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'Common/instrument.c').read_text(encoding='utf-8')
start = source.index('uint8_t Instrument_Command(')
brace = source.index('{', start)
depth, end = 1, brace + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
function = source[start:end]
prefix = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "instrument_core.h"
static struct { unsigned state, formatted; uint32_t samples; CaptureHeader header; } instrument;
static struct { unsigned ram_ok, nand_ok; } board_memory;
static struct { unsigned usb_trans_flag; } logic_adc_info;
static uint16_t instrument_data[CAPTURE_MAX_BYTES/2];
static int busy;
static int Instrument_Busy(void) { return busy; }
'''
main = r'''
int main(void) {
    uint8_t out[34], req[4]; uint32_t off;
    instrument.samples=1;
    memset(instrument_data,0x5a,sizeof instrument_data);
    const uint32_t bad[]={0,1,3,CAPTURE_MAX_BYTES+1,CAPTURE_MAX_BYTES+2,UINT32_MAX};
    off=0; memcpy(req,&off,4);
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];++i) {
        instrument.header.bytes=bad[i];
        memset(out,0xa5,sizeof out);
        assert(Instrument_Command(0xc2,req,out+1)==1);
        assert(out[3]==2 && out[0]==0xa5 && out[33]==0xa5);
    }
    instrument.header.bytes=CAPTURE_MAX_BYTES;
    for(off=0;off<CAPTURE_MAX_BYTES;++off) {
        memcpy(req,&off,4); memset(out,0xa5,sizeof out);
        assert(Instrument_Command(0xc2,req,out+1)==1);
        unsigned n=CAPTURE_MAX_BYTES-off; if(n>29) n=29;
        assert(out[2]==1+n && out[3]==0);
        for(unsigned i=0;i<n;++i) assert(out[4+i]==0x5a);
        for(unsigned i=4+n;i<33;++i) assert(out[i]==0);
        assert(out[0]==0xa5 && out[33]==0xa5);
    }
    off=UINT32_MAX; memcpy(req,&off,4);
    Instrument_Command(0xc2,req,out+1); assert(out[3]==2);
    off=CAPTURE_MAX_BYTES; memcpy(req,&off,4);
    Instrument_Command(0xc2,req,out+1); assert(out[3]==2);
    busy=1; Instrument_Command(0xc2,req,out+1); assert(out[3]==1); busy=0;
    logic_adc_info.usb_trans_flag=1;
    Instrument_Command(0xc2,req,out+1); assert(out[3]==1);
    logic_adc_info.usb_trans_flag=0;
    off=sizeof(CaptureHeader)-1; memcpy(req,&off,4);
    Instrument_Command(0xc1,req,out+1); assert(out[2]==2 && out[3]==0);
    Instrument_Command(0xc0,req,out+1); assert(out[2]==9 && out[3]==0);
    assert(Instrument_Command(0xbf,req,out+1)==0);
    puts("PASS: 65536 export offsets, invalid metadata, sentinel, busy and header bounds");
}
'''
with tempfile.TemporaryDirectory(prefix='la-export-test-') as folder:
    c = Path(folder) / 'test.c'
    c.write_text(prefix + function + main, encoding='utf-8')
    exe = Path(folder) / 'test.exe'
    subprocess.run([r'C:\msys64\mingw64\bin\gcc.exe', '-std=c99', '-Wall', '-Wextra',
                    '-Werror', '-fanalyzer', '-I', str(root/'Common'), str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
