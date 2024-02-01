////////////////////////////////////////////////////////////////////////////////
// MIDI handling
////////////////////////////////////////////////////////////////////////////////

#include "midi.h"

#include "storage.h"
#include "ui.h"
#include "arp.h"
#include "seq.h"

#include "../xnormidi/midi_device.h"
#include "../xnormidi/midi.h"

#define MIDI_BASE_STEPPED_CC 56
#define MIDI_BASE_COARSE_CC 16
#define MIDI_BASE_FINE_CC 80

#define MIDI_NOTE_TRANSPOSE_OFFSET -12

static MidiDevice midi[MIDI_PORT_COUNT];

uint16_t midiCombineBytes(uint8_t first, uint8_t second)
{
   uint16_t _14bit;
   _14bit = (uint16_t)second;
   _14bit <<= 7;
   _14bit |= (uint16_t)first;
   return _14bit;
}

static int8_t midiFilterChannel(uint8_t channel)
{
	return settings.midiReceiveChannel<0 || (channel&MIDI_CHANMASK)==settings.midiReceiveChannel;
}

static void noteOnEvent(MidiDevice * device, uint8_t channel, uint8_t note, uint8_t velocity)
{
	int16_t intNote;
	
	if(!midiFilterChannel(channel))
		return;
	
#ifdef DEBUG_
	print("midi note on  ");
	phex(note);
	print("\n");
#endif

	note+=MIDI_NOTE_TRANSPOSE_OFFSET;
	
	if(ui_isTransposing())
	{
		ui_setTranspose(note-MIDDLE_C_NOTE);
		return;
	}
	
	if(arp_getMode()==amOff)
	{
		// sequencer note input		
		if(seq_getMode(0)==smRecording || seq_getMode(1)==smRecording)
			seq_inputNote(note,velocity!=0);

		intNote=note+ui_getTranspose();
		intNote=MAX(0,MIN(127,intNote));
		assigner_assignNote(intNote,velocity!=0,(((uint32_t)velocity+1)<<9)-1,1);
	}
	else
	{
		arp_assignNote(note,velocity!=0);
	}
}

static void noteOffEvent(MidiDevice * device, uint8_t channel, uint8_t note, uint8_t velocity)
{
	int16_t intNote;
	
	if(!midiFilterChannel(channel))
		return;
	
#ifdef DEBUG_
	print("midi note off ");
	phex(note);
	print("\n");
#endif

	if(arp_getMode()==amOff)
	{
		// sequencer note input		
		if(seq_getMode(0)==smRecording || seq_getMode(1)==smRecording)
			seq_inputNote(note,0);

		intNote=note+MIDI_NOTE_TRANSPOSE_OFFSET+ui_getTranspose();
		intNote=MAX(0,MIN(127,intNote));
		assigner_assignNote(intNote,0,0,1);
	}
	else
	{
		arp_assignNote(note+MIDI_NOTE_TRANSPOSE_OFFSET,0);
	}
}

static void ccEvent(MidiDevice * device, uint8_t channel, uint8_t control, uint8_t value)
{
	int16_t param;
	int8_t change=0;
	
	if(!midiFilterChannel(channel))
		return;
	
#ifdef DEBUG_
	print("midi cc ");
	phex(control);
	print(" value ");
	phex(value);
	print("\n");
#endif

	if(control==1) // modwheel
	{
		synth_wheelEvent(0,value<<9,2);
	}
	else if(control==64) // hold pedal
	{
		assigner_holdEvent(value);
		return;
	}
	else if(control==120) // all sound off
	{
		assigner_panicOff();
		return;
	}
	else if(control==123) // all notes off
	{
		assigner_allKeysOff();
		return;
	}
	
	if(control>=MIDI_BASE_COARSE_CC && control<MIDI_BASE_COARSE_CC+cpCount)
	{
		param=control-MIDI_BASE_COARSE_CC;
		
		if((currentPreset.continuousParameters[param]>>9)!=value)
		{
			currentPreset.continuousParameters[param]&=0x01fc;
			currentPreset.continuousParameters[param]|=(uint16_t)value<<9;
			change=1;	
		}
	}
	else if(control>=MIDI_BASE_FINE_CC && control<MIDI_BASE_FINE_CC+cpCount)
	{
		param=control-MIDI_BASE_FINE_CC;

		if(((currentPreset.continuousParameters[param]>>2)&0x7f)!=value)
		{
			currentPreset.continuousParameters[param]&=0xfe00;
			currentPreset.continuousParameters[param]|=(uint16_t)value<<2;
			change=1;	
		}
	}
	else if(control>=MIDI_BASE_STEPPED_CC && control<MIDI_BASE_STEPPED_CC+spCount)
	{
		param=control-MIDI_BASE_STEPPED_CC;
		uint8_t v;
		
		v=value>>(7-steppedParametersBits[param]);
		
		if(currentPreset.steppedParameters[param]!=v)
		{
			currentPreset.steppedParameters[param]=v;
			change=1;	
		}
	}

	if(change)
	{
		ui_setPresetModified(1);
		synth_refreshFullState();
	}
}

static void progchangeEvent(MidiDevice * device, uint8_t channel, uint8_t program)
{
	if(!midiFilterChannel(channel))
		return;
	
	uint16_t newPresetNumber=(((settings.presetNumber/100)%10)*100)+program;

	if(program<100 && newPresetNumber!=settings.presetNumber)
	{
		settings.presetNumber=newPresetNumber;
		settings_save();		
		
		if(!preset_loadCurrent(newPresetNumber))
			preset_loadDefault(1);
		ui_setPresetModified(0);	

		synth_refreshWaveforms(0);
		synth_refreshWaveforms(1);
		synth_refreshWaveforms(2);
		synth_refreshFullState();
	}
}

static void pitchBendEvent(MidiDevice * device, uint8_t channel, uint8_t v1, uint8_t v2)
{
	if(!midiFilterChannel(channel))
		return;

	int16_t value;
	
	value=midiCombineBytes(v1,v2);
	value-=0x2000;
	value<<=2;
	
	synth_wheelEvent(value,0,1);
}

static void chanpressureEvent(MidiDevice * device, uint8_t channel, uint8_t pressure)
{
	if(!midiFilterChannel(channel))
		return;

	synth_pressureEvent(pressure<<9);
}

static void realtimeEvent(MidiDevice * device, uint8_t event)
{
	synth_realtimeEvent(event);
}

void midi_init(void)
{
	for(uint8_t port=0;port<MIDI_PORT_COUNT;++port)
	{
		midi_device_init(&midi[port]);
		midi_register_noteon_callback(&midi[port],noteOnEvent);
		midi_register_noteoff_callback(&midi[port],noteOffEvent);
		midi_register_cc_callback(&midi[port],ccEvent);
		midi_register_progchange_callback(&midi[port],progchangeEvent);
		midi_register_pitchbend_callback(&midi[port],pitchBendEvent);
		midi_register_chanpressure_callback(&midi[port],chanpressureEvent);
		midi_register_realtime_callback(&midi[port],realtimeEvent);
	}
}

void midi_update(void)
{
	for(uint8_t port=0;port<MIDI_PORT_COUNT;++port)
		midi_device_process(&midi[port]);
}

void midi_newData(uint8_t port, uint8_t data)
{
	midi_device_input(&midi[port],1,&data);
}
