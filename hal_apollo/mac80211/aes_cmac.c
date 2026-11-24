/*
 * AES-128-CMAC with TLen 16 for IEEE 802.11w BIP
 * Copyright 2008, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>

#include "aes_cmac.h"

#define AAD_LEN 20

/*
 * MIC over AAD || data, with the last IEEE80211_CMAC_MIC_LEN bytes of
 * data (the MMIE's MIC field) taken as zero.
 */
void ieee80211_aes_cmac(const struct aes_cmac_key *key, const u8 *aad,
			const u8 *data, size_t data_len, u8 *mic)
{
	static const u8 zero[IEEE80211_CMAC_MIC_LEN];
	struct aes_cmac_ctx ctx;
	u8 out[AES_BLOCK_SIZE];

	aes_cmac_init(&ctx, key);
	aes_cmac_update(&ctx, aad, AAD_LEN);
	aes_cmac_update(&ctx, data, data_len - IEEE80211_CMAC_MIC_LEN);
	aes_cmac_update(&ctx, zero, IEEE80211_CMAC_MIC_LEN);
	aes_cmac_final(&ctx, out);
	memcpy(mic, out, IEEE80211_CMAC_MIC_LEN);
	memzero_explicit(out, sizeof(out));
}
