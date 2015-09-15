#ifndef SYNTH_H
#define SYNTH_H

#include "main.h"
#include "utils.h"

#define SYNTH_VOICE_COUNT 6
//#define SYNTH_MASTER_CLOCK SystemCoreClock
#define SYNTH_MASTER_CLOCK 120000000

#define TICKER_1S 500

#define CVMUX_PORT_ABC 0
#define CVMUX_PIN_A 19
#define CVMUX_PIN_B 20
#define CVMUX_PIN_C 21

#define CVMUX_PORT_CARD0 0
#define CVMUX_PIN_CARD0 22

#define CVMUX_PORT_CARD1 0
#define CVMUX_PIN_CARD1 10

#define CVMUX_PORT_CARD2 4
#define CVMUX_PIN_CARD2 29

#define CVMUX_PORT_VCA 4
#define CVMUX_PIN_VCA 28

typedef enum {cvAVol=0,cvBVol=1,cvCutoff=2,cvResonance=3,cvAPitch=4,cvBPitch=5,cvAmp=6,cvMasterLeft=7,cvMasterRight=8} cv_t; 

typedef enum
{
	modOff=0,modPitch=1,modFil=2,modAmp=3
} modulationTarget_t;

typedef enum
{
	wmOff=0,wmAliasing=1,wmWidth=2,wmFrequency=3
} wmodTarget_t;

typedef enum
{
	otNone=0,otA=1,otB=2,otBoth=3
} oscTarget_t;

typedef enum
{
	smInternal=0,smMIDI=1,smTape=2
} syncMode_t;

void synth_assignerEvent(uint8_t note, int8_t gate, int8_t voice, uint16_t velocity, int8_t legato); // voice -1 is unison
void synth_uartEvent(uint8_t data);
void synth_updateDACsEvent(int32_t start, int32_t count);
void synth_wheelEvent(int16_t bend, uint16_t modulation, uint8_t mask);
void synth_realtimeEvent(uint8_t midiEvent);

void synth_timerInterrupt(void);

void synth_init(void);
void synth_update(void);

// synth.c internal api
void refreshFullState(void);
void refreshWaveNames(int8_t ab);
void refreshWaveforms(int8_t ab);
int8_t appendBankName(int8_t ab, char * path);
int8_t appendWaveName(int8_t ab, char * path);
int getBankCount(void);
int getWaveCount(int8_t ab);

extern volatile uint32_t currentTick; // 500hz

#endif
