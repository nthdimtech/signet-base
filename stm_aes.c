#include "stm_aes.h"

#include "print.h"

#include <nettle/aes.h>

void stm_aes_init()
{
}

void stm_aes_encrypt(u8 *key, const u8 *din, u8 *dout)
{
	struct aes128_ctx ctx;
	aes128_set_encrypt_key(&ctx, key);
	aes128_encrypt(&ctx, 16, dout, din);
}

static void xor_block(const u8 *src_block, const u8 *mask, u8 *dst_block)
{
	int i;
	for (i = 0; i < 16; i++) {
		dst_block[i] = src_block[i] ^ mask[i];
	}
}

void stm_aes_encrypt_cbc(u8 *key, int n_blocks, const u8 *iv, const u8 *din, u8 *dout)
{
	int i;
	for (i = 0; i < n_blocks; i++) {
		u8 temp[128];
		xor_block(din, iv, temp);
		stm_aes_encrypt(key, temp, dout);
		iv = dout;
		din += 16;
		dout += 16;
	}
}

void stm_aes_decrypt_cbc(u8 *key, int n_blocks, const u8 *iv, const u8 *din, u8 *dout)
{
	int i;
	for (i = 0; i < n_blocks; i++) {
		u8 temp[128];
		stm_aes_decrypt(key, din, temp);
		xor_block(temp, iv, dout);
		iv = din;
		din += 16;
		dout += 16;
	}
}

void stm_aes_decrypt(u8 *key, const u8 *din, u8 *dout)
{
	struct aes128_ctx ctx;
	aes128_set_decrypt_key(&ctx, key);
	aes128_decrypt(&ctx, 16,  dout, din);
}
