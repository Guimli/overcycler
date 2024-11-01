#ifndef LFO_H
#define	LFO_H

#include "synth.h"

typedef enum
{
	lsPulse=0,lsTri=1,lsRand=2,lsSine=3,lsNoise=4,lsSaw=5,lsRevSaw=6
} lfoShape_t;

struct lfo_s
{
	uint32_t noise;
	
	uint32_t halfPeriodCounter;
	uint32_t phase;
	int32_t speed;
	int32_t increment;	
	
	uint16_t levelCV,bpmCV;
	int16_t output;
	
	int8_t speedShift;
	
	lfoShape_t shape;
	uint8_t halfPeriodLimit;
};

void lfo_setCVs(struct lfo_s * lfo, uint16_t spd, uint16_t lvl);
void lfo_setShape(struct lfo_s * lfo, lfoShape_t shape, uint8_t halfPeriods); // set halfPeriods to 0 for unlimited periods
void lfo_setSpeedShift(struct lfo_s * lfo, int8_t shift);

int16_t lfo_getOutput(struct lfo_s * lfo);
const char * lfo_shapeName(lfoShape_t shape);

void lfo_reset(struct lfo_s * lfo);

void lfo_init(struct lfo_s * lfo);
void lfo_update(struct lfo_s * lfo);

#endif	/* LFO_H */

