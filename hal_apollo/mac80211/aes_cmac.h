/*
 * Copyright 2008, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef AES_CMAC_H
#define AES_CMAC_H

#include <crypto/aes-cbc-macs.h>

#define IEEE80211_CMAC_KEY_LEN	16
#define IEEE80211_CMAC_MIC_LEN	8

void ieee80211_aes_cmac(const struct aes_cmac_key *key, const u8 *aad,
			const u8 *data, size_t data_len, u8 *mic);
#endif /* AES_CMAC_H */
