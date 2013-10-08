////////////////////////////////////////////////////////////////////////////////
// Presets and settings storage, relies on low level page storage system
////////////////////////////////////////////////////////////////////////////////

#include "storage.h"
#include "lfo.h"
#include "wtosc.h"

// increment this each time the binary format is changed
#define STORAGE_VERSION 0

#define STORAGE_MAGIC 0x006116a5
#define STORAGE_MAX_SIZE 512

#define SETTINGS_PAGE_COUNT 2
#define SETTINGS_PAGE ((STORAGE_SIZE/STORAGE_PAGE_SIZE)-4)

const uint8_t steppedParametersBits[spCount] = 
{
	/*ABank*/5,
	/*AWave*/5,
	/*ABaseWMod*/2,
	/*AFilWMod*/1,
	/*BBank*/5,
	/*BWave*/5,
	/*BBaseWMod*/2,
	/*BWFilMod*/1,
	/*LFOShape*/3,
	/*LFOShift*/2,
	/*LFOTargets*/2,
	/*FilEnvSlow*/1,
	/*AmpEnvSlow*/1,
	/*BenderSemitones*/4,
	/*BenderTarget*/2,
	/*ModwheelShift*/3,
	/*ModwheelTarget*/1,
	/*Unison*/1,
	/*AssignerPriority*/2,
	/*ChromaticPitch*/2,
};

struct settings_s settings;
struct preset_s currentPreset;

static struct
{
	uint8_t buffer[STORAGE_MAX_SIZE];
	uint8_t * bufPtr;
	uint8_t version;
} storage;

static uint32_t storageRead32(void)
{
	uint32_t v;
	memcpy(&v,storage.bufPtr,sizeof(v));
	storage.bufPtr+=sizeof(v);
	return v;
}

static uint16_t storageRead16(void)
{
	uint16_t v;
	memcpy(&v,storage.bufPtr,sizeof(v));
	storage.bufPtr+=sizeof(v);
	return v;
}

/*
static int16_t storageReadS16(void)
{
	int16_t v;
	memcpy(&v,storage.bufPtr,sizeof(v));
	storage.bufPtr+=sizeof(v);
	return v;
}
*/

static uint8_t storageRead8(void)
{
	uint8_t v;
	v=*(uint8_t*)storage.bufPtr;
	storage.bufPtr+=sizeof(v);
	return v;
}

static int8_t storageReadS8(void)
{
	int8_t v;
	v=*(int8_t*)storage.bufPtr;
	storage.bufPtr+=sizeof(v);
	return v;
}

static void storageWrite32(uint32_t v)
{
	memcpy(storage.bufPtr,&v,sizeof(v));
	storage.bufPtr+=sizeof(v);
}

static void storageWrite16(uint16_t v)
{
	memcpy(storage.bufPtr,&v,sizeof(v));
	storage.bufPtr+=sizeof(v);
}

/*
static void storageWriteS16(int16_t v)
{
	memcpy(storage.bufPtr,&v,sizeof(v));
	storage.bufPtr+=sizeof(v);
}
*/

static void storageWrite8(uint8_t v)
{
	*(uint8_t*)storage.bufPtr=v;
	storage.bufPtr+=sizeof(v);
}

static void storageWriteS8(int8_t v)
{
	*(int8_t*)storage.bufPtr=v;
	storage.bufPtr+=sizeof(v);
}

static LOWERCODESIZE int8_t storageLoad(uint16_t pageIdx, uint8_t pageCount)
{
	uint16_t i;
	
	memset(storage.buffer,0,sizeof(storage.buffer));
	
	for (i=0;i<pageCount;++i)
		storage_read(pageIdx+i,&storage.buffer[STORAGE_PAGE_SIZE*i]);
	
	storage.bufPtr=storage.buffer;
	storage.version=0;

	if(storageRead32()!=STORAGE_MAGIC)
	{
#ifdef DEBUG
		print("Error: bad page !\n"); 
#endif	
		return 0;
	}

	storage.version=storageRead8();
	
	return 1;
}

static LOWERCODESIZE void storagePrepareStore(void)
{
	memset(storage.buffer,0,sizeof(storage.buffer));
	storage.bufPtr=storage.buffer;
	storage.version=STORAGE_VERSION;
	
	storageWrite32(STORAGE_MAGIC);
	storageWrite8(storage.version);
}

static LOWERCODESIZE void storageFinishStore(uint16_t pageIdx, uint8_t pageCount)
{
	if((storage.bufPtr-storage.buffer)>sizeof(storage.buffer))
	{
#ifdef DEBUG
		print("Error: writing too much data to storage !\n"); 
#endif	
		return;
	}
	
	uint16_t i;
	
	for (i=0;i<pageCount;++i)
		storage_write(pageIdx+i,&storage.buffer[STORAGE_PAGE_SIZE*i]);
}

LOWERCODESIZE int8_t settings_load(void)
{
	int8_t i,j;
	
	BLOCK_INT
	{
		if(!storageLoad(SETTINGS_PAGE,SETTINGS_PAGE_COUNT))
			return 0;

		if (storage.version<1)
			return 0;

		// v1
		for(j=0;j<TUNER_OCTAVE_COUNT;++j)
			for(i=0;i<TUNER_CV_COUNT;++i)
				settings.tunes[j][i]=storageRead16();

		settings.presetNumber=storageRead16();
		settings.presetMode=storageRead8();
		settings.midiReceiveChannel=storageReadS8();
		
		if (storage.version<2)
			return 1;

		// v2

		settings.voiceMask=storageRead8();
		settings.midiSendChannel=storageReadS8();
		
		if (storage.version<3)
			return 1;

		// v3
		
		// ...
	
	
	}
	
	return 1;
}

LOWERCODESIZE void settings_save(void)
{
	int8_t i,j;
	
	BLOCK_INT
	{
		storagePrepareStore();

		// v1

		for(j=0;j<TUNER_OCTAVE_COUNT;++j)
			for(i=0;i<TUNER_CV_COUNT;++i)
				storageWrite16(settings.tunes[j][i]);

		storageWrite16(settings.presetNumber);
		storageWrite8(settings.presetMode);
		storageWriteS8(settings.midiReceiveChannel);
		
		// v2
		
		storageWrite8(settings.voiceMask);
		storageWriteS8(settings.midiSendChannel);

		// v3
		
		// ...

		// this must stay last
		storageFinishStore(SETTINGS_PAGE,SETTINGS_PAGE_COUNT);
	}
}

LOWERCODESIZE int8_t preset_loadCurrent(uint16_t number)
{
	int8_t i;
	
	BLOCK_INT
	{
		storageLoad(number,1);

		if (storage.version<1)
			return 0;

		// v1
		
		continuousParameter_t cp;
		for(cp=0;cp<=cpCount;++cp)
			currentPreset.continuousParameters[cp]=storageRead16();

		steppedParameter_t sp;
		for(sp=0;sp<=spCount;++sp)
			currentPreset.steppedParameters[sp]=storageRead8();

		for(i=0;i<SYNTH_VOICE_COUNT;++i)
			currentPreset.voicePattern[i]=storageRead8();
	}
	
	return 1;
}

LOWERCODESIZE void preset_saveCurrent(uint16_t number)
{
	int8_t i;
	
	BLOCK_INT
	{
		storagePrepareStore();

		// v1
		
		continuousParameter_t cp;
		for(cp=0;cp<=cpCount;++cp)
			storageWrite16(currentPreset.continuousParameters[cp]);

		steppedParameter_t sp;
		for(sp=0;sp<=spCount;++sp)
			storageWrite8(currentPreset.steppedParameters[sp]);
		
		for(i=0;i<SYNTH_VOICE_COUNT;++i)
			storageWrite8(currentPreset.voicePattern[i]);
		
		// this must stay last
		storageFinishStore(number,1);
	}
}

LOWERCODESIZE void storage_export(uint16_t number, uint8_t * buf, int16_t * size)
{
	int16_t actualSize;

	BLOCK_INT
	{
		storageLoad(number,1);

		// don't export trailing zeroes		
		
		actualSize=STORAGE_PAGE_SIZE;
		while(storage.buffer[actualSize-1]==0)
			--actualSize;
		
		buf[0]=number;		
		memcpy(&buf[1],storage.buffer,actualSize);
		*size=actualSize+1;
	}
}

LOWERCODESIZE void storage_import(uint16_t number, uint8_t * buf, int16_t size)
{
	BLOCK_INT
	{
		memset(storage.buffer,0,sizeof(storage.buffer));
		memcpy(storage.buffer,buf,size);
		storage.bufPtr=storage.buffer+size;
		storageFinishStore(number,1);
	}
}

LOWERCODESIZE void preset_loadDefault(int8_t makeSound)
{
	int8_t i;

	BLOCK_INT
	{
		memset(&currentPreset,0,sizeof(currentPreset));

		currentPreset.continuousParameters[cpUnisonDetune]=256;
		currentPreset.continuousParameters[cpMasterTune]=UINT16_MAX/2;
		currentPreset.continuousParameters[cpMasterLeft]=UINT16_MAX/2;
		currentPreset.continuousParameters[cpMasterRight]=UINT16_MAX/2;

		currentPreset.continuousParameters[cpBFineFreq]=UINT16_MAX/2;
		currentPreset.continuousParameters[cpCutoff]=UINT16_MAX;
		currentPreset.continuousParameters[cpFilEnvAmt]=UINT16_MAX/2;
		currentPreset.continuousParameters[cpAmpVelocity]=UINT16_MAX/2;
		currentPreset.continuousParameters[cpLFOPitchAmt]=UINT16_MAX/16;
		currentPreset.continuousParameters[cpLFOFreq]=UINT16_MAX/2;
				
		currentPreset.steppedParameters[spBenderSemitones]=5;
		currentPreset.steppedParameters[spBenderTarget]=modPitch;
		currentPreset.steppedParameters[spModwheelShift]=1;
		currentPreset.steppedParameters[spChromaticPitch]=2; // octave
		currentPreset.steppedParameters[spAssignerPriority]=apLast;
		currentPreset.steppedParameters[spLFOShape]=lsTri;
		currentPreset.steppedParameters[spLFOTargets]=otBoth;
		currentPreset.steppedParameters[spLFOShift]=1;
		
		for(i=0;i<SYNTH_VOICE_COUNT;++i)
			currentPreset.voicePattern[i]=(i==0)?0:ASSIGNER_NO_NOTE;	
		
		if(makeSound)
		{
			currentPreset.continuousParameters[cpAVol]=UINT16_MAX/2;
			currentPreset.continuousParameters[cpAmpSus]=UINT16_MAX;
		}

		// temp
		currentPreset.continuousParameters[cpMasterLeft]=15000;
		currentPreset.continuousParameters[cpMasterRight]=15000;
		currentPreset.continuousParameters[cpUnisonDetune]=0;
//		currentPreset.continuousParameters[cpLFOPitchAmt]=0;
//		currentPreset.continuousParameters[cpLFOFilAmt]=35000;
//		currentPreset.continuousParameters[cpLFOFreq]=2000;
//		currentPreset.continuousParameters[cpCutoff]=30000;
//		currentPreset.continuousParameters[cpFilKbdAmt]=20000;
		currentPreset.continuousParameters[cpAmpVelocity]=UINT16_MAX*1/2;
//		currentPreset.continuousParameters[cpAmpAtt]=55000;
//		currentPreset.continuousParameters[cpAmpSus]=40000;
//		currentPreset.continuousParameters[cpAmpDec]=35000;
//		currentPreset.continuousParameters[cpAmpRel]=55000;
//		currentPreset.continuousParameters[cpAFreq]=96*WTOSC_CV_SEMITONE;
//		currentPreset.continuousParameters[cpAVol]=UINT16_MAX*2/4;
//		currentPreset.continuousParameters[cpBVol]=UINT16_MAX*4/4;
//		currentPreset.continuousParameters[cpBFineFreq]=UINT16_MAX/2+2048;
	}
}

LOWERCODESIZE void settings_loadDefault(void)
{
	BLOCK_INT
	{
		memset(&settings,0,sizeof(settings));
		
		settings.midiReceiveChannel=-1;
		settings.voiceMask=0x3f;
		
		tuner_init(); // use theoretical tuning
	}
}
