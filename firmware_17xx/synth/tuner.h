#ifndef TUNER_H
#define	TUNER_H

#include "synth.h"

#define TUNER_CV_COUNT SYNTH_VOICE_COUNT
#define TUNER_OCTAVE_COUNT 8

uint16_t tuner_computeCVFromNote(int8_t voice, uint8_t note, uint8_t nextInterp, cv_t cv);

void tuner_init(void);
void tuner_tuneSynth(void);

#endif	/* TUNER_H */

