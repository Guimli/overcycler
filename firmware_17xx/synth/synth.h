#ifndef SYNTH_H
#define SYNTH_H

#include "main.h"
#include "utils.h"

#define SYNTH_VOICE_COUNT 6
//#define SYNTH_MASTER_CLOCK SystemCoreClock
#define SYNTH_MASTER_CLOCK 120000000

#define SYNTH_DEFAULT_WAVE_BANK "AKWF_bw_perfectwaves"
#define SYNTH_DEFAULT_WAVE_NAME "AKWF_saw.wav"

#define TICKER_1S 500

// Some constants for 16 bit ranges */
#define FULL_RANGE UINT16_MAX
#define HALF_RANGE (FULL_RANGE/2+1)
#define HALF_RANGE_L (65536UL*HALF_RANGE) // i.e. HALF_RANGE<<16, as uint32_t

#define SCANNER_BASE_NOTE 12
#define MIDDLE_C_NOTE 60

typedef enum {
	cvAVol=0,cvBVol=1,cvCutoff=2,cvResonance=3,cvAPitch=4,cvBPitch=5,cvACrossOver=6,cvAmp=7,cvNoiseVol=8,
			
	// /!\ this must stay last
	cvCount
} cv_t; 

typedef enum
{
	modOff=0,modPitch=1,modFilter=2,modVolume=3,modCrossOver=4
} modulationTarget_t;

typedef enum
{
	wmOff=0,wmAliasing=1,wmWidth=2,wmFrequency=3,wmCrossOver=4
} wmodTarget_t;

typedef enum
{
	otNone=0,otA=1,otB=2,otBoth=3
} oscTarget_t;

typedef enum
{
	smInternal=0,smMIDI=1,smTape=2
} syncMode_t;

void synth_assignerEvent(uint8_t note, int8_t gate, int8_t voice, uint16_t velocity, int8_t flags);
void synth_uartEvent(uint8_t data);
void synth_updateDACsEvent(int32_t start, int32_t count);
void synth_updateCVsEvent(void);
void synth_wheelEvent(int16_t bend, uint16_t modulation, uint8_t mask);
void synth_realtimeEvent(uint8_t midiEvent);

void synth_timerInterrupt(void);

void synth_init(void);
void synth_update(void);

// synth.c internal api
void synth_refreshFullState(void);
int8_t synth_refreshBankNames(int8_t sort);
void synth_refreshCurWaveNames(int8_t abx, int8_t sort);
void synth_refreshWaveforms(int8_t abx);
void synth_reloadLegacyBankWaveIndexes(int8_t abx, int8_t loadDefault, int8_t sort);
int synth_getBankCount(void);
int synth_getCurWaveCount(void);
int8_t synth_getBankName(int bankIndex, char * res);
int8_t synth_getWaveName(int waveIndex, char * res);
int32_t synth_getVisualEnvelope(int8_t voice);

extern volatile uint32_t currentTick; // 500hz
extern const uint16_t extClockDividers[16];

#endif
