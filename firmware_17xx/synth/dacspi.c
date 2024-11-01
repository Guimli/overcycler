///////////////////////////////////////////////////////////////////////////////
// 12bit voice DACs communication through SPI
///////////////////////////////////////////////////////////////////////////////

#include "dacspi.h"

#include "LPC177x_8x.h"
#include "lpc177x_8x_gpdma.h"
#include "lpc177x_8x_gpio.h"
#include "lpc177x_8x_pinsel.h"

#define DMA_CHANNEL_UART2_TX__T3_MAT_0 14

#define SPIMUX_PORT_ABC 1
#define SPIMUX_PIN_A 14
#define SPIMUX_PIN_B 15
#define SPIMUX_PIN_C 16

#define SPIMUX_VAL(c,b,a) (((a)<<SPIMUX_PIN_A)|((b)<<SPIMUX_PIN_B)|((c)<<SPIMUX_PIN_C))

#define DACSPI_CMD_SET_A 0x7000
#define DACSPI_CMD_SET_B 0xf000

#define DACSPI_DMACONFIG \
		GPDMA_DMACCxConfig_E | \
		GPDMA_DMACCxConfig_SrcPeripheral(DMA_CHANNEL_UART2_TX__T3_MAT_0) | \
		GPDMA_DMACCxConfig_TransferType(2) | \
		GPDMA_DMACCxConfig_ITC

static EXT_RAM GPDMA_LLI_Type lli[DACSPI_BUFFER_COUNT*DACSPI_CHANNEL_COUNT][3];
static EXT_RAM GPDMA_LLI_Type cvLli[DACSPI_BUFFER_COUNT][4];
static EXT_RAM volatile uint8_t marker;
static EXT_RAM uint8_t markerSource[DACSPI_BUFFER_COUNT];

static const uint32_t spiMuxCommandsConst[DACSPI_CHANNEL_COUNT][3] =
{
	{SPIMUX_VAL(1,0,1),(uint32_t)&LPC_GPIO1->FIOSET,5},
	{SPIMUX_VAL(1,0,0),(uint32_t)&LPC_GPIO1->FIOCLR,1},
	{SPIMUX_VAL(0,1,0),(uint32_t)&LPC_GPIO1->FIOSET,3},
	{SPIMUX_VAL(0,0,1),(uint32_t)&LPC_GPIO1->FIOCLR,2},
	{SPIMUX_VAL(1,0,0),(uint32_t)&LPC_GPIO1->FIOSET,6},
	{SPIMUX_VAL(0,1,0),(uint32_t)&LPC_GPIO1->FIOCLR,4},
	{SPIMUX_VAL(1,0,0),(uint32_t)&LPC_GPIO1->FIOCLR,0},
};


static const uint16_t oscChannelCommand[SYNTH_VOICE_COUNT*2] = 
{
	DACSPI_CMD_SET_A,DACSPI_CMD_SET_B,
	DACSPI_CMD_SET_A,DACSPI_CMD_SET_B,
	DACSPI_CMD_SET_A,DACSPI_CMD_SET_B,
	DACSPI_CMD_SET_A,DACSPI_CMD_SET_B,
	DACSPI_CMD_SET_A,DACSPI_CMD_SET_B,
	DACSPI_CMD_SET_A,DACSPI_CMD_SET_B,
};

static struct
{
	uint16_t oscCommands[DACSPI_BUFFER_COUNT][SYNTH_VOICE_COUNT*2];
	uint32_t cvCommands[DACSPI_BUFFER_COUNT];
	uint32_t spiMuxCommands[DACSPI_CHANNEL_COUNT][3];
	uint16_t cr0Pre, cr0Post, sselPre, sselPost;
	int curSet;
} dacspi EXT_RAM;

__attribute__ ((used)) void DMA_IRQHandler(void)
{
	static uint8_t phase=0;
	
	LPC_GPDMA->IntTCClear=LPC_GPDMA->IntTCStat; // acknowledge interrupt

	// when second half is playing, update first and vice-versa
	dacspi.curSet=(marker>=DACSPI_BUFFER_COUNT/2)?0:DACSPI_BUFFER_COUNT/2;

	// update CVs and DACs (in 2 sets of 16)
	
	synth_updateCVsEvent();
	synth_updateOscsEvent(dacspi.curSet,DACSPI_BUFFER_COUNT/4);

	dacspi.curSet+=DACSPI_CV_COUNT;
	synth_updateCVsEvent();
	synth_updateOscsEvent(dacspi.curSet,DACSPI_BUFFER_COUNT/4);

	// update timer @ 500Hz

	synth_tickTimerEvent(phase);

	++phase;
	if(phase>=4)
		phase=0;
}

static void buildLLIs(int buffer, int channel)
{
	int lliPos=buffer*DACSPI_CHANNEL_COUNT+channel;
	int muxIndex=lliPos%DACSPI_CHANNEL_COUNT;
	int muxChannel=dacspi.spiMuxCommands[muxIndex][2];
	int8_t isCVChannel=muxChannel==0;
	
	lli[lliPos][0].SrcAddr=(uint32_t)&dacspi.spiMuxCommands[muxIndex][0];
	lli[lliPos][0].DstAddr=dacspi.spiMuxCommands[muxIndex][1];
	lli[lliPos][0].Control=
		GPDMA_DMACCxControl_TransferSize(1) |
		GPDMA_DMACCxControl_SWidth(2) |
		GPDMA_DMACCxControl_DWidth(2);

	if(isCVChannel)
	{
		lli[lliPos][0].NextLLI=(uint32_t)&cvLli[buffer][0];

		cvLli[buffer][0].NextLLI=(uint32_t)&cvLli[buffer][1];
		cvLli[buffer][1].NextLLI=(uint32_t)&lli[lliPos][1];

		cvLli[buffer][0].SrcAddr=(uint32_t)&dacspi.cr0Pre;
		cvLli[buffer][1].SrcAddr=(uint32_t)&dacspi.sselPre;

		cvLli[buffer][0].DstAddr=(uint32_t)&LPC_SSP2->CR0;
		cvLli[buffer][1].DstAddr=(uint32_t)&LPC_IOCON->P1_8;

		cvLli[buffer][0].Control=
		cvLli[buffer][1].Control=
			GPDMA_DMACCxControl_TransferSize(1) |
			GPDMA_DMACCxControl_SWidth(1) |
			GPDMA_DMACCxControl_DWidth(1);
	}
	else
	{
		lli[lliPos][0].NextLLI=(uint32_t)&lli[lliPos][1];
	}
	
	lli[lliPos][1].DstAddr=(uint32_t)&LPC_SSP2->DR;
	lli[lliPos][1].NextLLI=(uint32_t)&lli[lliPos][2];
	lli[lliPos][1].Control=
		GPDMA_DMACCxControl_TransferSize(1) |
		GPDMA_DMACCxControl_SWidth(2) |
		GPDMA_DMACCxControl_DWidth(1);

	if(isCVChannel)
	{
		lli[lliPos][1].SrcAddr=(uint32_t)&dacspi.cvCommands[buffer];
	}
	else
	{
		int voice=muxChannel-1;
		
		lli[lliPos][1].SrcAddr=(uint32_t)&dacspi.oscCommands[buffer][voice*2];
	}
	
	lli[lliPos][2].SrcAddr=(uint32_t)&markerSource[buffer];
	lli[lliPos][2].DstAddr=(uint32_t)&marker;
	lli[lliPos][2].Control=
		GPDMA_DMACCxControl_TransferSize(isCVChannel?DACSPI_CV_CHANNEL_WAIT_STATES:DACSPI_OSC_CHANNEL_WAIT_STATES) |
		GPDMA_DMACCxControl_SWidth(0) |
		GPDMA_DMACCxControl_DWidth(0);

	if(isCVChannel)
	{
		lli[lliPos][2].NextLLI=(uint32_t)&cvLli[buffer][2];
		
		cvLli[buffer][2].NextLLI=(uint32_t)&cvLli[buffer][3];
		cvLli[buffer][3].NextLLI=(uint32_t)&lli[(lliPos+1)%(DACSPI_BUFFER_COUNT*DACSPI_CHANNEL_COUNT)][0];
		
		cvLli[buffer][2].SrcAddr=(uint32_t)&dacspi.sselPost;
		cvLli[buffer][3].SrcAddr=(uint32_t)&dacspi.cr0Post;

		cvLli[buffer][2].DstAddr=(uint32_t)&LPC_IOCON->P1_8;
		cvLli[buffer][3].DstAddr=(uint32_t)&LPC_SSP2->CR0;

		cvLli[buffer][2].Control=
		cvLli[buffer][3].Control=
			GPDMA_DMACCxControl_TransferSize(1) |
			GPDMA_DMACCxControl_SWidth(1) |
			GPDMA_DMACCxControl_DWidth(1);
	}
	else
	{
		lli[lliPos][2].NextLLI=(uint32_t)&lli[(lliPos+1)%(DACSPI_BUFFER_COUNT*DACSPI_CHANNEL_COUNT)][0];
	}
}

FORCEINLINE void dacspi_setOscValue(int32_t buffer, int channel, uint16_t value)
{
	dacspi.oscCommands[buffer][channel]=(value>>4)|oscChannelCommand[channel];
}

FORCEINLINE void dacspi_setCVValue(int channel, uint16_t value, int8_t noDblBuf)
{
	uint32_t cmd=(0x100|((channel&0xf)<<4)|(value>>12))|((value&0xfff)<<16);
	
	if(noDblBuf)
	{
		for(int set=0;set<DACSPI_BUFFER_COUNT;set+=DACSPI_CV_COUNT)
			dacspi.cvCommands[channel+set]=cmd;
	}
	else
	{
		dacspi.cvCommands[channel+dacspi.curSet]=cmd;
	}
}

void dacspi_init(void)
{
	int i,j;
	
	// reset
	
	TIM_Cmd(LPC_TIM3,DISABLE);
	LPC_GPDMACH0->CConfig=0;
	SSP_Cmd(LPC_SSP2,DISABLE);

	memset(&dacspi,0,sizeof(dacspi));
	memcpy(dacspi.spiMuxCommands,spiMuxCommandsConst,sizeof(spiMuxCommandsConst));

	// init SPI mux

	GPIO_SetDir(SPIMUX_PORT_ABC,1<<SPIMUX_PIN_A,1); // A
	GPIO_SetDir(SPIMUX_PORT_ABC,1<<SPIMUX_PIN_B,1); // B
	GPIO_SetDir(SPIMUX_PORT_ABC,1<<SPIMUX_PIN_C,1); // C
	LPC_GPIO1->FIOMASK&=~SPIMUX_VAL(1,1,1);
	LPC_GPIO1->FIOSET=SPIMUX_VAL(1,1,1);
	
	// SSP pins

	PINSEL_ConfigPin(1,0,4);
	PINSEL_ConfigPin(1,1,4);
	PINSEL_ConfigPin(1,8,4);
	
	// SSP

	SSP_CFG_Type SSP_ConfigStruct;
	SSP_ConfigStructInit(&SSP_ConfigStruct);
	SSP_ConfigStruct.Databit=SSP_DATABIT_16;
	SSP_ConfigStruct.ClockRate=20000000;
	SSP_Init(LPC_SSP2,&SSP_ConfigStruct);
	SSP_DMACmd(LPC_SSP2,SSP_DMA_TX,ENABLE);
	SSP_Cmd(LPC_SSP2,ENABLE);
	
	dacspi.cr0Pre=SSP_CPHA_SECOND|SSP_CPOL_HI|SSP_DATABIT_12|(LPC_SSP2->CR0&0xff00);
	dacspi.cr0Post=LPC_SSP2->CR0;

	dacspi.sselPre=0;
	dacspi.sselPost=4;
		
	LPC_GPIO1->FIOCLR=SPIMUX_VAL(1,1,1);
	GPIO_SetDir(SPIMUX_PORT_ABC,1<<8,1);
	GPIO_ClearValue(SPIMUX_PORT_ABC,1<<8);

	// prepare LLIs

	for(j=0;j<DACSPI_BUFFER_COUNT;++j)
	{
		markerSource[j]=j;
		for(i=0;i<DACSPI_CHANNEL_COUNT;++i)
			buildLLIs(j,i);
	}

	// interrupt triggers
	
	lli[(1)*DACSPI_CHANNEL_COUNT][0].Control|=GPDMA_DMACCxControl_I;
	lli[(DACSPI_BUFFER_COUNT/2+1)*DACSPI_CHANNEL_COUNT][0].Control|=GPDMA_DMACCxControl_I;
	
	// GPDMA & timer
	
	CLKPWR_ConfigPPWR(CLKPWR_PCONP_PCGPDMA,ENABLE);
	
	LPC_SC->MATRIXARB&=~0b11111111;
	LPC_SC->MATRIXARB|= 0b11001001; // give priority to the GPDMA controller over anything else to lower jitter
	
	LPC_SC->DMAREQSEL|=1<<DMA_CHANNEL_UART2_TX__T3_MAT_0;
	LPC_GPDMA->Config=GPDMA_DMACConfig_E;

	TIM_TIMERCFG_Type tim;
	
	tim.PrescaleOption=TIM_PRESCALE_TICKVAL;
	tim.PrescaleValue=1;
	
	TIM_Init(LPC_TIM3,TIM_TIMER_MODE,&tim);
	
	TIM_MATCHCFG_Type tm;
	
	tm.MatchChannel=0;
	tm.IntOnMatch=DISABLE;
	tm.ResetOnMatch=ENABLE;
	tm.StopOnMatch=DISABLE;
	tm.ExtMatchOutputType=0;
	tm.MatchValue=DACSPI_TIMER_MATCH;

	TIM_ConfigMatch(LPC_TIM3,&tm);
	
	NVIC_SetPriority(DMA_IRQn,1);
	NVIC_EnableIRQ(DMA_IRQn);

	// start
	
	TIM_Cmd(LPC_TIM3,ENABLE);
	
	LPC_GPDMACH0->CSrcAddr=lli[0][0].SrcAddr;
	LPC_GPDMACH0->CDestAddr=lli[0][0].DstAddr;
	LPC_GPDMACH0->CLLI=lli[0][0].NextLLI;
	LPC_GPDMACH0->CControl=lli[0][0].Control;

	LPC_GPDMACH0->CConfig=DACSPI_DMACONFIG;
	
	// wait until all CV DACs inits are processed
	while(marker!=markerSource[0]);
	while(marker!=markerSource[DACSPI_BUFFER_COUNT-1]);
	
	rprintf(0,"sampling at %d Hz, cv update at %d Hz\n",SYNTH_MASTER_CLOCK/DACSPI_TICK_RATE, DACSPI_UPDATE_HZ);
}