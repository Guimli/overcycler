///////////////////////////////////////////////////////////////////////////////
// 12bit voice DACs communication through SPI
///////////////////////////////////////////////////////////////////////////////

#include "dacspi.h"

static struct
{
	uint16_t prevCommands[DACSPI_CHANNEL_COUNT][2];
} dacspi;

void dacspi_init(void)
{
	memset(&dacspi,0,sizeof(dacspi));
	
	// SSP

	CLKPWR_SetPCLKDiv(CLKPWR_PCLKSEL_SSP0,CLKPWR_PCLKSEL_CCLK_DIV_1);
	CLKPWR_ConfigPPWR(CLKPWR_PCONP_PCSSP0,ENABLE);

	LPC_SSP0->CPSR=2;
	LPC_SSP0->CR0=0x0f|((3-1)<<8); // 16Bit SPI(0,0) 20Mhz
	LPC_SSP0->CR1=2; // Enable

	// SSP pins

	PINSEL_SetPinFunc(1,20,3);
	PINSEL_SetPinFunc(1,21,3);
	PINSEL_SetPinFunc(1,24,3);
	
	// /LDAC pins
	
	for(int i=0;i<DACSPI_CHANNEL_COUNT;++i)
	{
		PINSEL_SetPinFunc(2,i,0);
		GPIO_SetDir(2,1<<i,1);
		GPIO_SetValue(2,1<<i);
	}
}

inline void dacspi_sendCommand(uint8_t channel, uint16_t command)
{
	// send commands

	if(channel!=DACSPI_CV_CHANNEL)
	{
		dacspi.prevCommands[channel][command>>15]=command;
		LPC_SSP0->DR=dacspi.prevCommands[channel][0];
		LPC_SSP0->DR=dacspi.prevCommands[channel][1];
	}
	else
	{
		LPC_SSP0->DR=command;
	}

	// wait while SSP busy

	while(LPC_SSP0->SR&SSP_SR_BSY)
		__NOP();

	// send /LDAC pulse to load the DAC values

	uint8_t mask=1<<channel;

	LPC_GPIO2->FIOCLR0=mask;
	DELAY_100NS();
	LPC_GPIO2->FIOSET0=mask;
}
