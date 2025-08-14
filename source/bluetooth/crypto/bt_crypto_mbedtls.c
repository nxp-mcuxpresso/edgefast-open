/*
 * Copyright 2021, 2025 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>

#include "mbedtls/aes.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

#define LOG_LEVEL CONFIG_BT_CRYPTO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_crypto);

typedef struct bt_aes_128_cmac_state
{
    uint8_t k1[16];
    uint8_t k2[16];
    uint8_t x[16];
    uint8_t y[16];
    uint8_t key[16];
    uint8_t data[16];
    uint8_t len;
    bool busy;
} bt_aes_128_cmac_state_t;

SDK_ALIGN(static uint8_t out_aes_crypt_buff[MAX(16U, EDGEFAST_BT_CACHE_LINESIZE)], MAX(16U, EDGEFAST_BT_CACHE_LINESIZE));
static struct k_mutex lock;

static int bt_aes_128_encrypt(const uint8_t in[16], const uint8_t key[16], uint8_t out[16])
{
	mbedtls_aes_context ctx;

	LOG_DBG("AES 128 ECB");

	k_mutex_lock(&lock, K_FOREVER);
	mbedtls_aes_init(&ctx);

	if(0 != mbedtls_aes_setkey_enc(&ctx, (const unsigned char *)key, 128))
	{
		k_mutex_unlock(&lock);
		LOG_ERR("Failed to set AES_ECB Key");
		return -EIO;
	}

	if(0 != mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, (const unsigned char *)in, (unsigned char *)out_aes_crypt_buff))
	{
		k_mutex_unlock(&lock);
		LOG_ERR("Failed to encrypt data by using AES_ECB algorithm");
		return -EIO;
	}

	(void)memcpy(out, (unsigned char *)out_aes_crypt_buff, 16);
	mbedtls_aes_free(&ctx);
	k_mutex_unlock(&lock);
	return 0;
}

static void bt_crypto_left_shift_one_bit(uint8_t *input, uint8_t *output)
{
	int32_t i;
	uint8_t overflow = 0u;

	for ( i=15; i>=0; i-- )
	{
		output[i] = input[i] << 1u;
		output[i] |= overflow;
		overflow = ((input[i] & 0x80u) > 0u) ? 1u : 0u;
	}
}

static void bt_crypto_xor128(uint8_t *a, const uint8_t *b, uint8_t *out)
{
	uint32_t i;

	for (i=0u;i<16u; i++)
	{
		out[i] = a[i] ^ b[i];
	}
}

static void bt_crypto_padding(uint8_t *lastb, uint8_t *pad, uint32_t length)
{
	uint32_t j;

	/* original last block */
	for ( j=0u; j<16u; j++ ) {
		if ( j < length ) {
			pad[j] = lastb[j];
		} else if ( j == length ) {
			pad[j] = 0x80u;
		} else {
			pad[j] = 0x00u;
		}
	}
}

static int bt_aes_128_cmac_update(bt_aes_128_cmac_state_t *state, const uint8_t * data, size_t len)
{
	uint32_t n;
	uint32_t i;
	const uint8_t * input;

	n = 0;

	if ((state->len + len) > sizeof(state->data))
	{
		memcpy(&state->data[state->len], data, sizeof(state->data) - state->len);
		len -= sizeof(state->data) - state->len;
		input = &data[sizeof(state->data) - state->len];
		state->len = sizeof(state->data);
		n = (uint8_t) ((len) / sizeof(state->data)); /* n is number of rounds */
		if (sizeof(state->data) == len)
		{
			n = 0;
		}
	}
	else
	{
		memcpy(&state->data[state->len], data, len);
		state->len += len;
		return 0;
	}

	if (state->len > 0u)
	{
		bt_crypto_xor128(state->x, &state->data[0u], state->y); /* Y := Mi (+) X  */
		bt_aes_128_encrypt(state->y, state->key, state->x); /* X := AES-128(KEY, Y) */
		state->len = 0;
	}

	if (n > 0)
	{
		if (0 == (len - n * sizeof(state->data)))
		{
			n = n - 1;
		}
	}

	for (i = 0u; i < n; i++)
	{
		bt_crypto_xor128(state->x, &input[sizeof(state->data) * i], state->y); /* Y := Mi (+) X  */
		bt_aes_128_encrypt(state->y, state->key, state->x); /* X := AES-128(KEY, Y) */
	}

	state->len = len - n * sizeof(state->data);
	if (state->len > 0)
	{
		memcpy(state->data, &input[n * sizeof(state->data)], state->len);
	}

	return 0;
}

static int bt_aes_128_cmac_final(uint8_t tag[16], bt_aes_128_cmac_state_t *state)
{
	uint8_t M_last[16] = {0};
	uint8_t padded[16] = {0};

	if (sizeof(state->data) == state->len)
	{
		bt_crypto_xor128(&state->data[0], state->k1, M_last);
	}
	else
	{
		bt_crypto_padding(&state->data[0], padded, state->len);
		bt_crypto_xor128(padded, state->k2, M_last);
	}
	bt_crypto_xor128(state->x, M_last, state->y);
	bt_aes_128_encrypt(state->y, state->key, state->x);

	memcpy(tag, state->x, 16u);

	state->busy = false;
	return 0;
}

static int bt_aes_128_cmac_setup(bt_aes_128_cmac_state_t *state, const uint8_t key[16])
{
	uint8_t const_Rb[16] = {0x00u, 0x00u, 0x00u, 0x00u,
				0x00u, 0x00u, 0x00u, 0x00u,
				0x00u, 0x00u, 0x00u, 0x00u,
				0x00u, 0x00u, 0x00u, 0x87u};
	uint8_t L[16] = { 0 };
	uint8_t Z[16] = { 0 };
	uint8_t tmp[16] = {0};

	if (NULL == state)
	{
		return -EINVAL;
	}

	memset(state, 0, sizeof(*state));

	memcpy(state->key, key, sizeof(state->key));

	memset(Z, 0, sizeof(Z));
	bt_aes_128_encrypt(Z, key, L);

	if ( (L[0] & 0x80u) == 0u )
	{
		/* If MSB(L) = 0, then K1 = L << 1 */
		bt_crypto_left_shift_one_bit(L,state->k1);
	}
	else
	{
		/* Else K1 = ( L << 1 ) (+) Rb */
		bt_crypto_left_shift_one_bit(L,tmp);
		bt_crypto_xor128(tmp,const_Rb,state->k1);
	}

	if ( (state->k1[0] & 0x80u) == 0u )
	{
		bt_crypto_left_shift_one_bit(state->k1,state->k2);
	}
	else
	{
		bt_crypto_left_shift_one_bit(state->k1,tmp);
		bt_crypto_xor128(tmp,const_Rb,state->k2);
	}
	return 0;
}

static int bt_aes_128_cmac_be(const uint8_t *key, const uint8_t *in, size_t len, uint8_t *out)
{
	bt_aes_128_cmac_state_t state;

	LOG_DBG("key %s in %s", bt_hex(key, 16), bt_hex(in, len));

	bt_aes_128_cmac_setup(&state, key);
	bt_aes_128_cmac_update(&state, in, len);
	bt_aes_128_cmac_final(out, &state);

	LOG_DBG("out %s", bt_hex(out, 16));

	return 0;
}

int bt_crypto_aes_cmac(const uint8_t *key, const uint8_t *in, size_t len, uint8_t *out)
{
    return bt_aes_128_cmac_be(key, in, len, out);
}
