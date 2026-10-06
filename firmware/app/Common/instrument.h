#ifndef INSTRUMENT_H
#define INSTRUMENT_H
#include "instrument_core.h"
enum { INST_IDLE, INST_ACQUIRE, INST_CACHE, INST_READY, INST_SAVE, INST_LOAD,
       INST_MOUNT, INST_FORMAT, INST_ERROR };
typedef struct {
    uint8_t mode, channel, range[2], rate, depth, trigger, trigger_channel;
    uint8_t repeat, reserved;
    uint16_t trigger_code, zero[2][4];
} InstrumentSettings;
typedef struct {
    volatile uint8_t state;
    uint8_t formatted, mounted, history_count;
    uint16_t records, bad_blocks, progress, selected_record;
    uint32_t revision, samples, offset;
    uint16_t minimum, maximum, average;
    int error;
    CaptureHeader header;
    InstrumentSettings settings;
} Instrument;
extern Instrument instrument;
extern uint16_t instrument_data[CAPTURE_MAX_BYTES/2];
void Instrument_Init(void);
void Instrument_Process(void);
int Instrument_Start(void);
void Instrument_Cancel(void);
int Instrument_Busy(void);
int Instrument_Save(void);
int Instrument_Load(uint16_t index);
int Instrument_History(uint8_t age);
int Instrument_Format(void); /* must be explicitly confirmed by user */
int Instrument_Zero(void); /* grounded input only; never automatic */
int Instrument_SaveSettings(void);
void Instrument_ApplyTrigger(void);
uint8_t Instrument_LogicIRQ(uint8_t line,uint32_t length);
uint8_t Instrument_Command(uint8_t cmd,const uint8_t *request,uint8_t *response);
extern volatile uint8_t adc_snapshot;
#endif
