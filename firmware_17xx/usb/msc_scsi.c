/*
	LPCUSB, an USB device driver for LPC microcontrollers	
	Copyright (C) 2006 Bertrik Sikken (bertrik@sikken.nl)

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	1. Redistributions of source code must retain the above copyright
	   notice, this list of conditions and the following disclaimer.
	2. Redistributions in binary form must reproduce the above copyright
	   notice, this list of conditions and the following disclaimer in the
	   documentation and/or other materials provided with the distribution.
	3. The name of the author may not be used to endorse or promote products
	   derived from this software without specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
	IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
	OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
	IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, 
	INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
	NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
	DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
	THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
	(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
	THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/**
	@file

	This is the SCSI layer of the USB mass storage application example.
	This layer depends directly on the blockdev layer.
	
	Windows peculiarities:
	* Size of REQUEST SENSE CDB is 12 bytes instead of expected 6
	* Windows requires VERIFY(10) command to do a format.
	  This command is not mandatory in the SBC/SBC-2 specification.
*/


#include <string.h>		// memcpy

#include "usb_type.h"
#include "usb_debug.h"

#include "nand.h"
#include "diskio.h"
#include "msc_scsi.h"


#define BLOCKSIZE		NAND_EMULATED_SECTOR_SIZE

// SBC2 mandatory SCSI commands
#define	SCSI_CMD_TEST_UNIT_READY	0x00
#define SCSI_CMD_REQUEST_SENSE		0x03
#define SCSI_CMD_FORMAT_UNIT		0x04
#define SCSI_CMD_READ_6				0x08	/* not implemented yet */
#define SCSI_CMD_INQUIRY			0x12
#define SCSI_CMD_SEND_DIAGNOSTIC	0x1D	/* not implemented yet */
#define SCSI_CMD_READ_CAPACITY_10	0x25
#define SCSI_CMD_READ_10			0x28
#define SCSI_CMD_REPORT_LUNS		0xA0	/* not implemented yet */

// SBC2 optional SCSI commands
#define SCSI_CMD_WRITE_6			0x0A	/* not implemented yet */
#define SCSI_CMD_WRITE_10			0x2A
#define SCSI_CMD_VERIFY_10			0x2F	/* required for windows format */

// sense codes
#define WRITE_ERROR				0x030C00
#define READ_ERROR				0x031100
#define INVALID_CMD_OPCODE		0x052000
#define INVALID_FIELD_IN_CDB	0x052400

//	Sense code, which is set on error conditions
static U32			dwSense;	// hex: 00aabbcc, where aa=KEY, bb=ASC, cc=ASCQ

static const U8		abInquiry[] = {
	0x00,		// PDT = direct-access device
	0x80,		// removeable medium bit = set
	0x05,		// version = complies to SPC3
	0x02,		// response data format = SPC3
	0x1F,		// additional length
	0x00,
	0x00,
	0x00,
	'G','l','i','G','l','i','\'','s',	// vendor
	'O','v','e','r','c','y','c','l',	// product
	'e','r',' ','f','l','a','s','h',
	'3','.','0',' '						// revision
};

//	Data for "request sense" command. The 0xFF are filled in later
static const U8 abSense[] = { 0x70, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x0A, 
							  0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
							  0x00, 0x00 };

//	Buffer for holding one block of disk data
static U8 abBlockBuf[BLOCKSIZE];


typedef struct {
	U8		bOperationCode;
	U8		abLBA[3];
	U8		bLength;
	U8		bControl;
} TCDB6;


/*************************************************************************
	SCSIReset
	=========
		Resets any SCSI state
		
**************************************************************************/
void SCSIReset(void)
{
	dwSense = 0;
}


/*************************************************************************
	SCSIHandleCmd
	=============
		Verifies a SCSI CDB and indicates the direction and amount of data
		that the device wants to transfer.
		
	If this call fails, a sense code is set in dwSense.

	IN		pbCDB		Command data block
			iCDBLen		Command data block len
	OUT		*piRspLen	Length of intended response data:
			*pfDevIn	TRUE if data is transferred from device-to-host
	
	Returns a pointer to the data exchange buffer if successful,
	return NULL otherwise.
**************************************************************************/
U8 * SCSIHandleCmd(U8 *pbCDB, U8 iCDBLen, int *piRspLen, BOOL *pfDevIn)
{
	static const U8 aiCDBLen[] = {6, 10, 10, 0, 16, 12, 0, 0};
	int		i;
	TCDB6	*pCDB;
	U32		dwLen, dwLBA;
	U8		bGroupCode;
	
	pCDB = (TCDB6 *)pbCDB;
	
	// default direction is from device to host
	*pfDevIn = TRUE;
	
	// check CDB length
	bGroupCode = (pCDB->bOperationCode >> 5) & 0x7;
	if (iCDBLen < aiCDBLen[bGroupCode]) {
		DBG("Invalid CDB len (expected %d)!\n", aiCDBLen[bGroupCode]);
		return NULL;
	}

	switch (pCDB->bOperationCode) {

	// test unit ready (6)
	case SCSI_CMD_TEST_UNIT_READY:
		DBG("TEST UNIT READY\n");
		*piRspLen = 0;
		break;
	
	// request sense (6)
	case SCSI_CMD_REQUEST_SENSE:
		DBG("REQUEST SENSE (%06X)\n", dwSense);
		// check params
		*piRspLen = MIN(18, pCDB->bLength);
		break;
	
	case SCSI_CMD_FORMAT_UNIT:
		DBG("FORMAT UNIT %02X\n", pbCDB[1]);
		*piRspLen = 0;
		break;
	
	// inquiry (6)
	case SCSI_CMD_INQUIRY:
		DBG("INQUIRY\n");
		// see SPC3r23, 4.3.4.6
		*piRspLen = MIN(36, pCDB->bLength);
		break;
		
	// read capacity (10)
	case SCSI_CMD_READ_CAPACITY_10:
		DBG("READ CAPACITY\n");
		*piRspLen = 8;
		break;
		
	// read (10)
	case SCSI_CMD_READ_10:
		dwLBA = (pbCDB[2] << 24) | (pbCDB[3] << 16) | (pbCDB[4] << 8) | (pbCDB[5]);
		dwLen = (pbCDB[7] << 8) | pbCDB[8];
		DBG("READ10, LBA=%d, len=%d\n", dwLBA, dwLen);
		*piRspLen = dwLen * BLOCKSIZE;
		break;

	// write (10)
	case SCSI_CMD_WRITE_10:
		dwLBA = (pbCDB[2] << 24) | (pbCDB[3] << 16) | (pbCDB[4] << 8) | (pbCDB[5]);
		dwLen = (pbCDB[7] << 8) | pbCDB[8];
		DBG("WRITE10, LBA=%d, len=%d\n", dwLBA, dwLen);
		*piRspLen = dwLen * BLOCKSIZE;
		*pfDevIn = FALSE;
		break;

	case SCSI_CMD_VERIFY_10:
		dwLBA = (pbCDB[2] << 24) | (pbCDB[3] << 16) | (pbCDB[4] << 8) | (pbCDB[5]);
		dwLen = (pbCDB[7] << 8) | pbCDB[8];
		DBG("VERIFY10, LBA=%d, len=%d\n", dwLBA, dwLen);
		*piRspLen = 0;
		if ((pbCDB[1] & (1 << 1)) != 0) {
			// we don't support BYTCHK
			DBG("BYTCHK not supported\n");
			dwSense = INVALID_FIELD_IN_CDB;
			return NULL;
		}
		break;
	
	default:
		DBG("Unhandled SCSI: ");		
		for (i = 0; i < iCDBLen; i++) {
			DBG(" %02X", pbCDB[i]);
		}
		DBG("\n");
		// unsupported command
		dwSense = INVALID_CMD_OPCODE;
		*piRspLen = 0;
		return NULL;
	}
	
	return abBlockBuf;
}


/*************************************************************************
	SCSIHandleData
	==============
		Handles a block of SCSI data.
		
	IN		pbCDB		Command data block
			iCDBLen		Command data block len
	IN/OUT	pbData		Data buffer
	IN		dwOffset	Offset in data
	
	Returns a pointer to the next data to be exchanged if successful,
	returns NULL otherwise.
**************************************************************************/
U8 * SCSIHandleData(U8 *pbCDB, U8 iCDBLen, U8 *pbData, U32 dwOffset)
{
	TCDB6	*pCDB;
	U32		dwLBA;
	U32		dwBufPos, dwBlockNr;
	U32		dwDevSize, dwDevBlkCnt, dwMaxBlock;
	U16		dwDevBlkSize;
	
	pCDB = (TCDB6 *)pbCDB;
	
	switch (pCDB->bOperationCode) {

	// test unit ready
	case SCSI_CMD_TEST_UNIT_READY:
		if (dwSense != 0) {
			DBG("UNIT NOT READY!\n");
			return NULL;
		}
		break;
	
	// request sense
	case SCSI_CMD_REQUEST_SENSE:
		memcpy(pbData, abSense, 18);
		// fill in KEY/ASC/ASCQ
		pbData[2] = (dwSense >> 16) & 0xFF;
		pbData[12] = (dwSense >> 8) & 0xFF;
		pbData[13] = (dwSense >> 0) & 0xFF;
		// reset sense data
		dwSense = 0;
		break;
	
	case SCSI_CMD_FORMAT_UNIT:
		// nothing to do, ignore this command
		break;
	
	// inquiry
	case SCSI_CMD_INQUIRY:
		memcpy(pbData, abInquiry, sizeof(abInquiry));
		break;
		
	// read capacity
	case SCSI_CMD_READ_CAPACITY_10:
		// get size of drive (bytes)
		disk_ioctl(0, GET_SECTOR_SIZE, &dwDevBlkSize);
		disk_ioctl(0, GET_SECTOR_COUNT, &dwDevBlkCnt);
		dwDevSize = dwDevBlkCnt * dwDevBlkSize;
		// calculate highest LBA
		dwMaxBlock = (dwDevSize - 1) / BLOCKSIZE;
		
		pbData[0] = (dwMaxBlock >> 24) & 0xFF;
		pbData[1] = (dwMaxBlock >> 16) & 0xFF;
		pbData[2] = (dwMaxBlock >> 8) & 0xFF;
		pbData[3] = (dwMaxBlock >> 0) & 0xFF;
		pbData[4] = (BLOCKSIZE >> 24) & 0xFF;
		pbData[5] = (BLOCKSIZE >> 16) & 0xFF;
		pbData[6] = (BLOCKSIZE >> 8) & 0xFF;
		pbData[7] = (BLOCKSIZE >> 0) & 0xFF;
		break;
		
	// read10
	case SCSI_CMD_READ_10:
		dwLBA = (pbCDB[2] << 24) | (pbCDB[3] << 16) | (pbCDB[4] << 8) | (pbCDB[5]);

		// copy data from block buffer
		dwBufPos = (dwOffset & (BLOCKSIZE - 1));
		if (dwBufPos == 0) {
			// read new block
			dwBlockNr = dwLBA + (dwOffset / BLOCKSIZE);
			DBG("R");
			if (disk_read(0, abBlockBuf, dwBlockNr, 1) > 0) {
				dwSense = READ_ERROR;
				DBG("disk_read failed\n");
				return NULL;
			}
		}
		// return pointer to data
		return abBlockBuf + dwBufPos;

	// write10
	case SCSI_CMD_WRITE_10:
		dwLBA = (pbCDB[2] << 24) | (pbCDB[3] << 16) | (pbCDB[4] << 8) | (pbCDB[5]);
		
		// copy data to block buffer
		dwBufPos = ((dwOffset + 64) & (BLOCKSIZE - 1));
		if (dwBufPos == 0) {
			// write new block
			dwBlockNr = dwLBA + (dwOffset / BLOCKSIZE);
			DBG("W");
			if (disk_write(0, abBlockBuf, dwBlockNr, 1) > 0) {
				dwSense = WRITE_ERROR;
				DBG("disk_write failed\n");
				return NULL;
			}
		}
		// return pointer to next data
		return abBlockBuf + dwBufPos;
		
	case SCSI_CMD_VERIFY_10:
		// dummy implementation
		break;
		
	default:
		// unsupported command
		dwSense = INVALID_CMD_OPCODE;
		return NULL;
	}
	
	// default: return pointer to start of block buffer
	return abBlockBuf;
}


