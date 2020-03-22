// Copyright 2019 SoloKeys Developers
//
// Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
// http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
// http://opensource.org/licenses/MIT>, at your option. This file may not be
// copied, modified, or distributed except according to those terms.
/*
 *  Wrapper for crypto implementation on device
 *
 * */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "crypto.h"

#include "types.h"
#define AES_BLK_SIZE (16)
#include <nettle/sha2.h>
#include <nettle/aes.h>
#include <nettle/ecc.h>
#include <nettle/ecc-curve.h>
#include <nettle/ecdsa.h>
#include <nettle/hmac.h>

#include "ctap.h"
#include "device.h"
#include "log.h"
#include "rand.h"

const uint8_t attestation_cert_der[];
const uint16_t attestation_cert_der_size;
const uint8_t attestation_key[];
const uint16_t attestation_key_size;

struct sha256_ctx sha256_ctx;
struct hmac_sha256_ctx hmac_sha256_ctx;
static const struct ecc_curve * _es256_curve = NULL;
static const uint8_t * _signing_key = NULL;
static int _key_len = 0;

// Secrets for testing only
static uint8_t master_secret[64];

static uint8_t transport_secret[32];

static void buffer_from_limbs(u8 *buffer, const mp_limb_t *l, int n)
{
	const int s = sizeof(mp_limb_t);
	for (int i = 0; i < n; i++) {
		for (int j = 0; j < s; j++) {
			buffer[i*s + (s - j - 1)] = (l[n - i - 1]>>((j)*8)) & 0xff;
		}
	}
}

static void mpz_from_buffer(mpz_t *val, const struct ecc_curve *curve, const uint8_t *buffer)
{
	mpz_init(*val);
	mp_limb_t *l = mpz_limbs_write(*val, ecc_size(curve));

	const int s = sizeof(mp_limb_t);
	const int n = ecc_size(curve);
	for (int i = 0; i < n; i++) {
		mp_limb_t v = 0;
		for (int j = 0; j < s; j++) {
			v |= ((mp_limb_t)buffer[(n-i)*s - 1 - j]) << (j*8);
		}
		l[i] = v;
	}
	mpz_limbs_finish(*val, ecc_size(curve));
}

void crypto_sha256_init()
{
    sha256_init(&sha256_ctx);
}

void crypto_reset_master_secret()
{
    ctap_generate_rng(master_secret, 64);
    ctap_generate_rng(transport_secret, 32);
}

void crypto_load_master_secret(uint8_t * key)
{
    #if KEY_SPACE_BYTES < 96
    #error "need more key bytes"
    #endif
    memmove(master_secret, key, 64);
    memmove(transport_secret, key+64, 32);
}

void crypto_sha256_update(uint8_t * data, size_t len)
{
    sha256_update(&sha256_ctx, len, data);
}

void crypto_sha256_update_secret()
{
    sha256_update(&sha256_ctx, SHA256_BLOCK_SIZE, master_secret);
}

void crypto_sha256_final(uint8_t * hash)
{
    sha256_digest(&sha256_ctx, SHA256_DIGEST_SIZE, hash);
}

void crypto_sha256_hmac_update(const uint8_t *data, size_t length)
{
	hmac_sha256_update(&hmac_sha256_ctx, length, data);
}

void crypto_sha256_hmac_init(uint8_t * key, uint32_t klen, uint8_t * hmac)
{
    if (key == CRYPTO_MASTER_KEY)
    {
        key = master_secret;
        klen = sizeof(master_secret);
    }
    else if (key == CRYPTO_TRANSPORT_KEY)
    {
        key = transport_secret;
        klen = 32;
    }
    assert(klen <= 64);
    hmac_sha256_set_key(&hmac_sha256_ctx, klen, key);
}

void crypto_sha256_hmac_final(uint8_t * key, uint32_t klen, uint8_t * hmac)
{
    hmac_sha256_digest(&hmac_sha256_ctx, 32, hmac);
}

void crypto_ecc256_init()
{
    _es256_curve = nettle_get_secp_256r1();
}

void crypto_ecc256_load_attestation_key()
{
    _signing_key = attestation_key;
    _key_len = 32;
}

static int s_random_requested = 0;
static int s_random_served = 0;

void crypto_random_init()
{
	s_random_requested = 0;
	s_random_served = 0;
}

int crypto_random_get_requested()
{
	return s_random_requested;
}

int crypto_random_get_served()
{
	return s_random_served;
}

static void crypto_random_func(void *ctx, size_t length, uint8_t *dst)
{
	int avail = rand_avail();
	int i;
	uint32_t *dstw = (uint32_t *)dst;
	s_random_requested += length;
	for (i = 0; i < (length/4) && i < avail; i++) {
		dstw[i] = rand_get();
		s_random_served += 4;
	}
	for (; i < (length/4); i++) {
		dstw[i] = 0x80808080;
	}
}

int ctap_generate_rng(uint8_t * dst, size_t num)
{
	crypto_random_func(NULL, num, dst);
	return 1;
}

static void crypto_sign(const struct ecc_curve *curve, const uint8_t * data, int len, uint8_t * sig);

void crypto_ecc256_sign(uint8_t * data, int len, uint8_t * sig)
{
    crypto_sign(_es256_curve, data, len, sig);
}

static void crypto_sign(const struct ecc_curve *curve, const uint8_t * data, int len, uint8_t * sig)
{
	struct dsa_signature signature_pt;
	struct ecc_scalar signing_key_pt;
	dsa_signature_init(&signature_pt);

	mpz_t val;
	mpz_from_buffer(&val, _es256_curve, _signing_key);
	ecc_scalar_init(&signing_key_pt, _es256_curve);
	ecc_scalar_set(&signing_key_pt, val);
	ecdsa_sign(&signing_key_pt,
		NULL, crypto_random_func,
		len, data,
		&signature_pt);

	const mp_limb_t *rp = mpz_limbs_read(signature_pt.r);
	const mp_limb_t *sp = mpz_limbs_read(signature_pt.s);

	buffer_from_limbs(sig, rp, 8);
	buffer_from_limbs(sig + 32, sp, 8);
	ecc_scalar_clear(&signing_key_pt);
	dsa_signature_clear(&signature_pt);
}

void crypto_ecc256_load_key(uint8_t * data, int len, uint8_t * data2, int len2)
{
    static uint8_t privkey[32];
    generate_private_key(data, len, data2, len2, privkey);
    _signing_key = privkey;
    _key_len = 32;
}

typedef enum
{
    MBEDTLS_ECP_DP_NONE = 0,
    MBEDTLS_ECP_DP_SECP192R1,      /*!< 192-bits NIST curve  */
    MBEDTLS_ECP_DP_SECP224R1,      /*!< 224-bits NIST curve  */
    MBEDTLS_ECP_DP_SECP256R1,      /*!< 256-bits NIST curve  */
    MBEDTLS_ECP_DP_SECP384R1,      /*!< 384-bits NIST curve  */
    MBEDTLS_ECP_DP_SECP521R1,      /*!< 521-bits NIST curve  */
    MBEDTLS_ECP_DP_BP256R1,        /*!< 256-bits Brainpool curve */
    MBEDTLS_ECP_DP_BP384R1,        /*!< 384-bits Brainpool curve */
    MBEDTLS_ECP_DP_BP512R1,        /*!< 512-bits Brainpool curve */
    MBEDTLS_ECP_DP_CURVE25519,           /*!< Curve25519               */
    MBEDTLS_ECP_DP_SECP192K1,      /*!< 192-bits "Koblitz" curve */
    MBEDTLS_ECP_DP_SECP224K1,      /*!< 224-bits "Koblitz" curve */
    MBEDTLS_ECP_DP_SECP256K1,      /*!< 256-bits "Koblitz" curve */
} mbedtls_ecp_group_id;

void crypto_ecdsa_sign(uint8_t * data, int len, uint8_t * sig, int MBEDTLS_ECP_ID)
{
    const struct ecc_curve *curve = NULL;

    switch(MBEDTLS_ECP_ID)
    {
        case MBEDTLS_ECP_DP_SECP192R1:
            curve = nettle_get_secp_192r1();
            if (_key_len != 24)  goto fail;
            break;
        case MBEDTLS_ECP_DP_SECP224R1:
            curve = nettle_get_secp_224r1();
            if (_key_len != 28)  goto fail;
            break;
        case MBEDTLS_ECP_DP_SECP256R1:
            curve = nettle_get_secp_256r1();
            if (_key_len != 32)  goto fail;
            break;
        //HC_TODO: Nettle doesn't have this curve
        /*
        case MBEDTLS_ECP_DP_SECP256K1:
            curve = uECC_secp256k1();
            if (_key_len != 32)  goto fail;
            break;
        */
        default:
            printf2(TAG_ERR,"error, invalid ECDSA alg specifier\n");
            exit(1);
    }
    crypto_sign(curve, data, len, sig);
    return;
fail:
	printf2(TAG_ERR,"error, invalid key length\n");
	exit(1);
}

void generate_private_key(uint8_t * data, int len, uint8_t * data2, int len2, uint8_t * privkey)
{
	crypto_sha256_hmac_init(CRYPTO_MASTER_KEY, 0, privkey);
	crypto_sha256_hmac_update(data, len);
	crypto_sha256_hmac_update(data2, len2);
	crypto_sha256_hmac_update(master_secret, 32);
	crypto_sha256_hmac_final(CRYPTO_MASTER_KEY, 0, privkey);
}

static void scalar_from_key_buffer(const struct ecc_curve *curve, struct ecc_scalar *key, const uint8_t *key_buffer)
{
	mpz_t val;
	mpz_from_buffer(&val, curve, key_buffer);
	ecc_scalar_init(key, curve);
	ecc_scalar_set(key, val);
}

static void crypto_compute_public_key(const struct ecc_curve *curve, const uint8_t *privkey, uint8_t *pubkey)
{
	struct ecc_point pub_pt;
	struct ecc_scalar priv_scalar;
	scalar_from_key_buffer(curve, &priv_scalar, privkey);
	ecc_point_init(&pub_pt, curve);
	ecdsa_generate_pub_from_priv(&pub_pt, &priv_scalar);
	mpz_t x;
	mpz_t y;
	mpz_init(x);
	mpz_init(y);
	ecc_point_get(&pub_pt, x, y);
	const mp_limb_t *xp = mpz_limbs_read(x);
	const mp_limb_t *yp = mpz_limbs_read(y);
	int i;
	buffer_from_limbs(pubkey, xp, 8);
	buffer_from_limbs(pubkey + 32, yp, 8);
	mpz_clear(x);
	mpz_clear(y);
	ecc_scalar_clear(&priv_scalar);
	ecc_point_clear(&pub_pt);
}

void crypto_ecc256_derive_public_key(uint8_t * data, int len, uint8_t * x, uint8_t * y)
{
	uint8_t privkey[32];
	uint8_t pubkey[64];
	generate_private_key(data,len, NULL, 0, privkey);
	crypto_ecc256_compute_public_key(privkey, pubkey);
	memmove(x, pubkey, 32);
	memmove(y, pubkey + 32, 32);
}

void crypto_ecc256_compute_public_key(const uint8_t * privkey, uint8_t * pubkey)
{
    crypto_compute_public_key(_es256_curve, privkey, pubkey);
}

void crypto_load_external_key(uint8_t * key, int len)
{
    _signing_key = key;
    _key_len = len;
}

void crypto_ecc256_make_key_pair(uint8_t * pubkey, uint8_t * privkey)
{
	struct ecc_point pub_pt;
	struct ecc_scalar key_scalar;
	ecc_scalar_init(&key_scalar, _es256_curve);
	ecc_point_init(&pub_pt, _es256_curve);
	ecdsa_generate_keypair(&pub_pt,
		&key_scalar,
		NULL, crypto_random_func);
	mpz_t x;
	mpz_t y;
	mpz_init(x);
	mpz_init(y);
	ecc_point_get(&pub_pt, x, y);
	const mp_limb_t *xp = mpz_limbs_read(x);
	const mp_limb_t *yp = mpz_limbs_read(y);
	int i;
	buffer_from_limbs(pubkey, xp, 8);
	buffer_from_limbs(pubkey + 32, yp, 8);
	mpz_clear(x);
	mpz_clear(y);

	buffer_from_limbs(privkey, key_scalar.p, 8);
	ecc_scalar_clear(&key_scalar);
	ecc_point_clear(&pub_pt);
}

void crypto_ecc256_shared_secret(const uint8_t * pubkey, const uint8_t * privkey, uint8_t * shared_secret)
{
	struct ecc_point pubkey_point;
	mpz_t x, y;
	mpz_from_buffer(&x, _es256_curve, pubkey);
	mpz_from_buffer(&y, _es256_curve, pubkey + 32);
	ecc_point_init(&pubkey_point, _es256_curve);
	ecc_point_set(&pubkey_point, x, y);

	struct ecc_scalar privkey_scalar;
	scalar_from_key_buffer(_es256_curve, &privkey_scalar, privkey);

	struct ecc_point result;
	ecc_point_init(&result, _es256_curve);
	ecc_point_mul(&result, &privkey_scalar, &pubkey_point);

	mpz_t sx;
	mpz_t sy;
	mpz_init(sx);
	mpz_init(sy);
	ecc_point_get(&result, sx, sy);
	const mp_limb_t *sxp = mpz_limbs_read(sx);
	int i;
	buffer_from_limbs(shared_secret, sxp, 8);
	mpz_clear(sx);
	mpz_clear(sy);

	ecc_point_clear(&result);
	ecc_scalar_clear(&privkey_scalar);
	ecc_point_clear(&pubkey_point);
}

static struct aes256_ctx aes_ctx;
static uint8_t aes_ctxIv[16];
static uint8_t aes_key[32];
static uint8_t aes_decrypt_mode;
static uint8_t aes_encrypt_mode;

void crypto_aes256_init(uint8_t *key, uint8_t *nonce)
{
	aes_decrypt_mode = 0;
	aes_encrypt_mode = 0;
	if (key == CRYPTO_TRANSPORT_KEY) {
		memcpy(aes_key, transport_secret, 32);
	} else {
		memcpy(aes_key, key, 32);
	}
	if (nonce == NULL) {
		memset(aes_ctxIv, 0, 16);
	} else {
		memmove(aes_ctxIv, nonce, 16);
	}
}

// prevent round key recomputation
void crypto_aes256_reset_iv(uint8_t * nonce)
{
	if (nonce == NULL) {
		memset(aes_ctxIv, 0, 16);
	} else {
		memmove(aes_ctxIv, nonce, 16);
	}
}

//
// HC_TODO: Implement aes256-cbc encrypt/decrypt only once. At the moment the implementation
// in signet_aes.c can't work here as is. The code size cost should be minimal since we are
// mostly wrapping nettle's AES-256 code
//
void xor_block(const u8 *src_block, const u8 *mask, u8 *dst_block);

void crypto_aes256_encrypt(uint8_t * buf, int length)
{
	if (!aes_encrypt_mode) {
		aes_encrypt_mode = 1;
		aes_decrypt_mode = 0;
		aes256_set_encrypt_key(&aes_ctx, aes_key);
	}
	int n_blocks = length / AES_BLK_SIZE;
	assert((n_blocks * AES_BLK_SIZE) == length);
	uint8_t *iv = aes_ctxIv;
	for (int i = 0; i < n_blocks; i++) {
		u8 temp[AES_BLK_SIZE];
		xor_block(buf + AES_BLK_SIZE * i, iv, temp);
		aes256_encrypt(&aes_ctx, AES_BLK_SIZE, buf + AES_BLK_SIZE * i, temp);
		iv = buf + AES_BLK_SIZE * i;
		if (i == (n_blocks - 1)) {
			memcpy(aes_ctxIv, iv, AES_BLK_SIZE);
		}
	}
}

void crypto_aes256_decrypt(uint8_t * buf, int length)
{
	if (!aes_decrypt_mode) {
		aes_decrypt_mode = 1;
		aes_encrypt_mode = 0;
		aes256_set_decrypt_key(&aes_ctx, aes_key);
	}
	int n_blocks = length / AES_BLK_SIZE;
	uint8_t *iv = aes_ctxIv;
	assert((n_blocks * AES_BLK_SIZE) == length);
	for (int i = 0; i < n_blocks; i++) {
		u8 temp[AES_BLK_SIZE];
		u8 src_temp[AES_BLK_SIZE];
		memcpy(src_temp, buf + AES_BLK_SIZE * i, 16);
		aes256_decrypt(&aes_ctx, AES_BLK_SIZE, temp, buf + AES_BLK_SIZE * i);
		xor_block(temp, aes_ctxIv, buf + AES_BLK_SIZE * i);
		memcpy(aes_ctxIv, src_temp, 16);
	}
}


const uint8_t attestation_cert_der[] =
"\x30\x82\x01\xfb\x30\x82\x01\xa1\xa0\x03\x02\x01\x02\x02\x01\x00\x30\x0a\x06\x08"
"\x2a\x86\x48\xce\x3d\x04\x03\x02\x30\x2c\x31\x0b\x30\x09\x06\x03\x55\x04\x06\x13"
"\x02\x55\x53\x31\x0b\x30\x09\x06\x03\x55\x04\x08\x0c\x02\x4d\x44\x31\x10\x30\x0e"
"\x06\x03\x55\x04\x0a\x0c\x07\x54\x45\x53\x54\x20\x43\x41\x30\x20\x17\x0d\x31\x38"
"\x30\x35\x31\x30\x30\x33\x30\x36\x32\x30\x5a\x18\x0f\x32\x30\x36\x38\x30\x34\x32"
"\x37\x30\x33\x30\x36\x32\x30\x5a\x30\x7c\x31\x0b\x30\x09\x06\x03\x55\x04\x06\x13"
"\x02\x55\x53\x31\x0b\x30\x09\x06\x03\x55\x04\x08\x0c\x02\x4d\x44\x31\x0f\x30\x0d"
"\x06\x03\x55\x04\x07\x0c\x06\x4c\x61\x75\x72\x65\x6c\x31\x15\x30\x13\x06\x03\x55"
"\x04\x0a\x0c\x0c\x54\x45\x53\x54\x20\x43\x4f\x4d\x50\x41\x4e\x59\x31\x22\x30\x20"
"\x06\x03\x55\x04\x0b\x0c\x19\x41\x75\x74\x68\x65\x6e\x74\x69\x63\x61\x74\x6f\x72"
"\x20\x41\x74\x74\x65\x73\x74\x61\x74\x69\x6f\x6e\x31\x14\x30\x12\x06\x03\x55\x04"
"\x03\x0c\x0b\x63\x6f\x6e\x6f\x72\x70\x70\x2e\x63\x6f\x6d\x30\x59\x30\x13\x06\x07"
"\x2a\x86\x48\xce\x3d\x02\x01\x06\x08\x2a\x86\x48\xce\x3d\x03\x01\x07\x03\x42\x00"
"\x04\x45\xa9\x02\xc1\x2e\x9c\x0a\x33\xfa\x3e\x84\x50\x4a\xb8\x02\xdc\x4d\xb9\xaf"
"\x15\xb1\xb6\x3a\xea\x8d\x3f\x03\x03\x55\x65\x7d\x70\x3f\xb4\x02\xa4\x97\xf4\x83"
"\xb8\xa6\xf9\x3c\xd0\x18\xad\x92\x0c\xb7\x8a\x5a\x3e\x14\x48\x92\xef\x08\xf8\xca"
"\xea\xfb\x32\xab\x20\xa3\x62\x30\x60\x30\x46\x06\x03\x55\x1d\x23\x04\x3f\x30\x3d"
"\xa1\x30\xa4\x2e\x30\x2c\x31\x0b\x30\x09\x06\x03\x55\x04\x06\x13\x02\x55\x53\x31"
"\x0b\x30\x09\x06\x03\x55\x04\x08\x0c\x02\x4d\x44\x31\x10\x30\x0e\x06\x03\x55\x04"
"\x0a\x0c\x07\x54\x45\x53\x54\x20\x43\x41\x82\x09\x00\xf7\xc9\xec\x89\xf2\x63\x94"
"\xd9\x30\x09\x06\x03\x55\x1d\x13\x04\x02\x30\x00\x30\x0b\x06\x03\x55\x1d\x0f\x04"
"\x04\x03\x02\x04\xf0\x30\x0a\x06\x08\x2a\x86\x48\xce\x3d\x04\x03\x02\x03\x48\x00"
"\x30\x45\x02\x20\x18\x38\xb0\x45\x03\x69\xaa\xa7\xb7\x38\x62\x01\xaf\x24\x97\x5e"
"\x7e\x74\x64\x1b\xa3\x7b\xf7\xe6\xd3\xaf\x79\x28\xdb\xdc\xa5\x88\x02\x21\x00\xcd"
"\x06\xf1\xe3\xab\x16\x21\x8e\xd8\xc0\x14\xaf\x09\x4f\x5b\x73\xef\x5e\x9e\x4b\xe7"
"\x35\xeb\xdd\x9b\x6d\x8f\x7d\xf3\xc4\x3a\xd7";


const uint16_t attestation_cert_der_size = sizeof(attestation_cert_der)-1;


const uint8_t attestation_key[] = "\xcd\x67\xaa\x31\x0d\x09\x1e\xd1\x6e\x7e\x98\x92\xaa\x07\x0e\x19\x94\xfc\xd7\x14\xae\x7c\x40\x8f\xb9\x46\xb7\x2e\x5f\xe7\x5d\x30";
const uint16_t attestation_key_size = sizeof(attestation_key)-1;
