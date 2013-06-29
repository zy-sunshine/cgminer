/**
 *   tyz.c - cgminer worker for tyz 1.15x fpga board
 *
 *   Copyright (c) 2012 nelisky.btc@gmail.com
 *
 *   This work is based upon the Java SDK provided by tyz which is
 *   Copyright (C) 2009-2011 tyz GmbH.
 *   http://www.tyz.de
 *
 *   This work is based upon the icarus.c worker which was
 *   Copyright 2012 Luke Dashjr
 *   Copyright 2012 Xiangfu <xiangfu@openmobilefree.com>
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
#include "miner.h"
#include <unistd.h>
#include <sha2.h>
#include "libtyz.h"
#include "util.h"

#define GOLDEN_BACKLOG 5

struct device_drv tyz_drv;

// Forward declarations
static void tyz_disable(struct thr_info* thr);
static bool tyz_prepare(struct thr_info *thr);

static void tyz_selectFpga(struct libtyz_device* tyz)
{
	if (tyz->root->numberOfFpgas > 1) {
		if (tyz->root->selectedFpga != tyz->fpgaNum)
			mutex_lock(&tyz->root->mutex);
		libtyz_selectFpga(tyz);
	}
}

static void tyz_releaseFpga(struct libtyz_device* tyz)
{
	if (tyz->root->numberOfFpgas > 1) {
		tyz->root->selectedFpga = -1;
		mutex_unlock(&tyz->root->mutex);
	}
}

static void tyz_detect(void)
{
	int cnt;
	int i,j;
	int fpgacount;
	struct libtyz_dev_list **tyz_devices;
	struct libtyz_device *tyz_slave;
	struct cgpu_info *tyz;

	cnt = libtyz_scanDevices(&tyz_devices);
	if (cnt > 0)
		applog(LOG_WARNING, "Found %d tyz board%s", cnt, cnt > 1 ? "s" : "");

	for (i = 0; i < cnt; i++) {
		tyz = calloc(1, sizeof(struct cgpu_info));
		tyz->drv = &tyz_drv;
		tyz->device_tyz = tyz_devices[i]->dev;
		tyz->threads = 1;
		tyz->device_tyz->fpgaNum = 0;
		tyz->device_tyz->root = tyz->device_tyz;
		add_cgpu(tyz);

		fpgacount = libtyz_numberOfFpgas(tyz->device_tyz);

		if (fpgacount > 1)
			pthread_mutex_init(&tyz->device_tyz->mutex, NULL);

		for (j = 1; j < fpgacount; j++) {
			tyz = calloc(1, sizeof(struct cgpu_info));
			tyz->drv = &tyz_drv;
			tyz_slave = calloc(1, sizeof(struct libtyz_device));
			memcpy(tyz_slave, tyz_devices[i]->dev, sizeof(struct libtyz_device));
			tyz->device_tyz = tyz_slave;
			tyz->threads = 1;
			tyz_slave->fpgaNum = j;
			tyz_slave->root = tyz_devices[i]->dev;
			tyz_slave->repr[strlen(tyz_slave->repr) - 1] = ('1' + j);
			add_cgpu(tyz);
		}

		applog(LOG_WARNING,"%s: Found tyz (fpga count = %d) , mark as %d", tyz->device_tyz->repr, fpgacount, tyz->device_id);
	}

	if (cnt > 0)
		libtyz_freeDevList(tyz_devices);
}

static bool tyz_updateFreq(struct libtyz_device* tyz)
{
	int i, maxM, bestM;
	double bestR, r;

	for (i = 0; i < tyz->freqMaxM; i++)
		if (tyz->maxErrorRate[i + 1] * i < tyz->maxErrorRate[i] * (i + 20))
			tyz->maxErrorRate[i + 1] = tyz->maxErrorRate[i] * (1.0 + 20.0 / i);

	maxM = 0;
	while (maxM < tyz->freqMDefault && tyz->maxErrorRate[maxM + 1] < LIBtyz_MAXMAXERRORRATE)
		maxM++;
	while (maxM < tyz->freqMaxM && tyz->errorWeight[maxM] > 150 && tyz->maxErrorRate[maxM + 1] < LIBtyz_MAXMAXERRORRATE)
		maxM++;

	bestM = 0;
	bestR = 0;
	for (i = 0; i <= maxM; i++) {
		r = (i + 1 + (i == tyz->freqM? LIBtyz_ERRORHYSTERESIS: 0)) * (1 - tyz->maxErrorRate[i]);
		if (r > bestR) {
			bestM = i;
			bestR = r;
		}
	}

	if (bestM != tyz->freqM) {
		tyz_selectFpga(tyz);
		libtyz_setFreq(tyz, bestM);
		tyz_releaseFpga(tyz);
	}

	maxM = tyz->freqMDefault;
	while (maxM < tyz->freqMaxM && tyz->errorWeight[maxM + 1] > 100)
		maxM++;
	if ((bestM < (1.0 - LIBtyz_OVERHEATTHRESHOLD) * maxM) && bestM < maxM - 1) {
		tyz_selectFpga(tyz);
		libtyz_resetFpga(tyz);
		tyz_releaseFpga(tyz);
		applog(LOG_ERR, "%s: frequency drop of %.1f%% detect. This may be caused by overheating. FPGA is shut down to prevent damage.",
		       tyz->repr, (1.0 - 1.0 * bestM / maxM) * 100);
		return false;
	}
	return true;
}


static uint32_t tyz_checkNonce(struct work *work, uint32_t nonce)
{
	uint32_t *data32 = (uint32_t *)(work->data);
	unsigned char swap[80];
	uint32_t *swap32 = (uint32_t *)swap;
	unsigned char hash1[32];
	unsigned char hash2[32];
	uint32_t *hash2_32 = (uint32_t *)hash2;
	int i;

	swap32[76/4] = htonl(nonce);

	for (i = 0; i < 76 / 4; i++)
		swap32[i] = swab32(data32[i]);

	sha2(swap, 80, hash1);
	sha2(hash1, 32, hash2);

	return htonl(hash2_32[7]);
}

static int64_t tyz_scanhash(struct thr_info *thr, struct work *work,
                              __maybe_unused int64_t max_nonce)
{
	struct libtyz_device *tyz;
	unsigned char sendbuf[44];
	int i, j, k;
	uint32_t *backlog;
	int backlog_p = 0, backlog_max;
	uint32_t *lastnonce;
	uint32_t nonce, noncecnt = 0;
	bool overflow, found;
	struct libtyz_hash_data hdata[GOLDEN_BACKLOG];

	if (thr->cgpu->deven == DEV_DISABLED)
		return -1;

	tyz = thr->cgpu->device_tyz;

	memcpy(sendbuf, work->data + 64, 12);
	memcpy(sendbuf + 12, work->midstate, 32);

	tyz_selectFpga(tyz);
	i = libtyz_sendHashData(tyz, sendbuf);
	if (i < 0) {
		// Something wrong happened in send
		applog(LOG_ERR, "%s: Failed to send hash data with err %d, retrying", tyz->repr, i);
		nmsleep(500);
		i = libtyz_sendHashData(tyz, sendbuf);
		if (i < 0) {
			// And there's nothing we can do about it
			tyz_disable(thr);
			applog(LOG_ERR, "%s: Failed to send hash data with err %d, giving up", tyz->repr, i);
			tyz_releaseFpga(tyz);
			return -1;
		}
	}
	tyz_releaseFpga(tyz);

	applog(LOG_DEBUG, "%s: sent hashdata", tyz->repr);

	lastnonce = calloc(1, sizeof(uint32_t)*tyz->numNonces);
	if (lastnonce == NULL) {
		applog(LOG_ERR, "%s: failed to allocate lastnonce[%d]", tyz->repr, tyz->numNonces);
		return -1;
	}

	/* Add an extra slot for detecting dupes that lie around */
	backlog_max = tyz->numNonces * (2 + tyz->extraSolutions);
	backlog = calloc(1, sizeof(uint32_t) * backlog_max);
	if (backlog == NULL) {
		applog(LOG_ERR, "%s: failed to allocate backlog[%d]", tyz->repr, backlog_max);
		return -1;
	}

	overflow = false;
	int count = 0;
	int validNonces = 0;
	double errorCount = 0;

	applog(LOG_DEBUG, "%s: entering poll loop", tyz->repr);
	while (!(overflow || thr->work_restart)) {
		count++;

		int sleepcount = 0;
		while (thr->work_restart == 0 && sleepcount < 25) {
			nmsleep(10);
			sleepcount += 1;
		}

		if (thr->work_restart) {
			applog(LOG_DEBUG, "%s: New work detected", tyz->repr);
			break;
		}

		tyz_selectFpga(tyz);
		i = libtyz_readHashData(tyz, &hdata[0]);
		if (i < 0) {
			// Something wrong happened in read
			applog(LOG_ERR, "%s: Failed to read hash data with err %d, retrying", tyz->repr, i);
			nmsleep(500);
			i = libtyz_readHashData(tyz, &hdata[0]);
			if (i < 0) {
				// And there's nothing we can do about it
				tyz_disable(thr);
				applog(LOG_ERR, "%s: Failed to read hash data with err %d, giving up", tyz->repr, i);
				free(lastnonce);
				free(backlog);
				tyz_releaseFpga(tyz);
				return -1;
			}
		}
		tyz_releaseFpga(tyz);

		if (thr->work_restart) {
			applog(LOG_DEBUG, "%s: New work detected", tyz->repr);
			break;
		}

		tyz->errorCount[tyz->freqM] *= 0.995;
		tyz->errorWeight[tyz->freqM] = tyz->errorWeight[tyz->freqM] * 0.995 + 1.0;

		for (i = 0; i < tyz->numNonces; i++) {
			nonce = hdata[i].nonce;
			if (nonce > noncecnt)
				noncecnt = nonce;
			if (((0xffffffff - nonce) < (nonce - lastnonce[i])) || nonce < lastnonce[i]) {
				applog(LOG_DEBUG, "%s: overflow nonce=%08x lastnonce=%08x", tyz->repr, nonce, lastnonce[i]);
				overflow = true;
			} else
				lastnonce[i] = nonce;

			if (tyz_checkNonce(work, nonce) != (hdata->hash7 + 0x5be0cd19)) {
				applog(LOG_DEBUG, "%s: checkNonce failed for %08X", tyz->repr, nonce);

				// do not count errors in the first 500ms after sendHashData (2x250 wait time)
				if (count > 2) {
					thr->cgpu->hw_errors++;
					errorCount += (1.0 / tyz->numNonces);
				}
			}
			else
				validNonces++;


			for (j=0; j<=tyz->extraSolutions; j++) {
				nonce = hdata[i].goldenNonce[j];

				if (nonce == tyz->offsNonces) {
					continue;
				}

				// precheck the extraSolutions since they often fail
				if (j > 0 && tyz_checkNonce(work, nonce) != 0) {
					continue;
				}

				found = false;
				for (k = 0; k < backlog_max; k++) {
					if (backlog[k] == nonce) {
						found = true;
						break;
					}
				}
				if (!found) {
					applog(LOG_DEBUG, "%s: Share found N%dE%d", tyz->repr, i, j);
					backlog[backlog_p++] = nonce;

					if (backlog_p >= backlog_max)
						backlog_p = 0;

					work->blk.nonce = 0xffffffff;
					submit_nonce(thr, work, nonce);
					applog(LOG_DEBUG, "%s: submitted %08x", tyz->repr, nonce);
				}
			}
		}
	}

	// only add the errorCount if we had at least some valid nonces or
	// had no valid nonces in the last round
	if (errorCount > 0.0) {
		if (tyz->nonceCheckValid > 0 && validNonces == 0) {
			applog(LOG_ERR, "%s: resetting %.1f errors", tyz->repr, errorCount);
		}
		else {
			tyz->errorCount[tyz->freqM] += errorCount;
		}
	}

	// remember the number of valid nonces for the check in the next round
	tyz->nonceCheckValid = validNonces;

	tyz->errorRate[tyz->freqM] = tyz->errorCount[tyz->freqM] /	tyz->errorWeight[tyz->freqM] * (tyz->errorWeight[tyz->freqM] < 100? tyz->errorWeight[tyz->freqM] * 0.01: 1.0);
	if (tyz->errorRate[tyz->freqM] > tyz->maxErrorRate[tyz->freqM])
		tyz->maxErrorRate[tyz->freqM] = tyz->errorRate[tyz->freqM];

	if (!tyz_updateFreq(tyz)) {
		// Something really serious happened, so mark this thread as dead!
		free(lastnonce);
		free(backlog);
		
		return -1;
	}

	applog(LOG_DEBUG, "%s: exit %1.8X", tyz->repr, noncecnt);

	work->blk.nonce = 0xffffffff;

	free(lastnonce);
	free(backlog);

	return noncecnt;
}

static void tyz_statline_before(char *buf, struct cgpu_info *cgpu)
{
	if (cgpu->deven == DEV_ENABLED) {
		tailsprintf(buf, "%s-%d | ", cgpu->device_tyz->snString, cgpu->device_tyz->fpgaNum+1);
		tailsprintf(buf, "%0.1fMHz | ", cgpu->device_tyz->freqM1 * (cgpu->device_tyz->freqM + 1));
	}
}

static bool tyz_prepare(struct thr_info *thr)
{
	struct timeval now;
	struct cgpu_info *cgpu = thr->cgpu;
	struct libtyz_device *tyz = cgpu->device_tyz;

	cgtime(&now);
	get_datestamp(cgpu->init, &now);

	tyz_selectFpga(tyz);
	if (libtyz_configureFpga(tyz) != 0) {
		libtyz_resetFpga(tyz);
		tyz_releaseFpga(tyz);
		applog(LOG_ERR, "%s: Disabling!", thr->cgpu->device_tyz->repr);
		thr->cgpu->deven = DEV_DISABLED;
		return true;
	}
	tyz->freqM = tyz->freqMaxM+1;;
	//tyz_updateFreq(tyz);
	libtyz_setFreq(tyz, tyz->freqMDefault);
	tyz_releaseFpga(tyz);
	applog(LOG_DEBUG, "%s: prepare", tyz->repr);
	return true;
}

static void tyz_shutdown(struct thr_info *thr)
{
	if (thr->cgpu->device_tyz != NULL) {
		if (thr->cgpu->device_tyz->fpgaNum == 0)
			pthread_mutex_destroy(&thr->cgpu->device_tyz->mutex);  
		applog(LOG_DEBUG, "%s: shutdown", thr->cgpu->device_tyz->repr);
		libtyz_destroy_device(thr->cgpu->device_tyz);
		thr->cgpu->device_tyz = NULL;
	}
}

static void tyz_disable(struct thr_info *thr)
{
	struct cgpu_info *cgpu;

	applog(LOG_ERR, "%s: Disabling!", thr->cgpu->device_tyz->repr);
	cgpu = get_devices(thr->cgpu->device_id);
	cgpu->deven = DEV_DISABLED;
	tyz_shutdown(thr);
}

struct device_drv tyz_drv = {
	.drv_id = DRIVER_TYZ,
	.dname = "tyz",
	.name = "TYZ",
	.drv_detect = tyz_detect,
	.get_statline_before = tyz_statline_before,
	.thread_prepare = tyz_prepare,
	.scanhash = tyz_scanhash,
	.thread_shutdown = tyz_shutdown,
};

