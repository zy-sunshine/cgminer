/**
 *   libtyz.h - headers for tyz 1.15x fpga board support library
 *
 *   Copyright (c) 2012 nelisky.btc@gmail.com
 *
 *   This work is based upon the Java SDK provided by tyz which is
 *   Copyright (C) 2009-2011 tyz GmbH.
 *   http://www.tyz.de
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, see http://www.gnu.org/licenses/.
**/
#ifndef __LIBtyz_H__
#define __LIBtyz_H__

#include <libusb.h>

#define LIBtyz_MAX_DESCRIPTORS 512
#define LIBtyz_SNSTRING_LEN 10

#define LIBtyz_IDVENDOR 0x221A
#define LIBtyz_IDPRODUCT 0x0100

#define LIBtyz_MAXMAXERRORRATE 0.05
#define LIBtyz_ERRORHYSTERESIS 0.1
#define LIBtyz_OVERHEATTHRESHOLD 0.4

struct libtyz_fpgastate {
	bool fpgaConfigured;
	unsigned char fpgaChecksum;
	uint16_t fpgaBytes;
	unsigned char fpgaInitB;
	unsigned char fpgaFlashResult;
	bool fpgaFlashBitSwap;
};

struct libtyz_device {
	pthread_mutex_t	mutex;
	struct libtyz_device *root;
	int16_t fpgaNum;
	struct libusb_device_descriptor descriptor;
	libusb_device_handle *hndl; 
	unsigned char usbbus;
	unsigned char usbaddress;
	unsigned char snString[LIBtyz_SNSTRING_LEN+1];
	unsigned char productId[4];
	unsigned char fwVersion;
	unsigned char interfaceVersion;
	unsigned char interfaceCapabilities[6];
	unsigned char moduleReserved[12];
	uint8_t numNonces;
	uint16_t offsNonces;
	double freqM1;	
	uint8_t freqM;
	uint8_t freqMaxM;
	uint8_t freqMDefault;
	char* bitFileName;
	bool suspendSupported;
	double hashesPerClock;
	uint8_t extraSolutions;

	double errorCount[256];
	double errorWeight[256];
	double errorRate[256];
	double maxErrorRate[256];

	int16_t nonceCheckValid;

	int16_t numberOfFpgas;
	int selectedFpga;
	bool parallelConfigSupport;
	
	char repr[20];
};

struct libtyz_dev_list { 
	struct libtyz_device *dev;
	struct libtyz_dev_list *next;
};

struct libtyz_hash_data {
	uint32_t goldenNonce[2];
	uint32_t nonce;
	uint32_t hash7;
};

extern int libtyz_scanDevices (struct libtyz_dev_list ***devs);
extern void libtyz_freeDevList (struct libtyz_dev_list **devs);
extern int libtyz_prepare_device (struct libusb_device *dev, struct libtyz_device** tyz);
extern void libtyz_destroy_device (struct libtyz_device* tyz);
extern int libtyz_configureFpga (struct libtyz_device *dev);
extern int libtyz_setFreq (struct libtyz_device *tyz, uint16_t freq);
extern int libtyz_sendHashData (struct libtyz_device *tyz, unsigned char *sendbuf);
extern int libtyz_readHashData (struct libtyz_device *tyz, struct libtyz_hash_data nonces[]);
extern int libtyz_resetFpga (struct libtyz_device *tyz);
extern int libtyz_selectFpga(struct libtyz_device *tyz);
extern int libtyz_numberOfFpgas(struct libtyz_device *tyz);

#endif /* __LIBtyz_H__ */
