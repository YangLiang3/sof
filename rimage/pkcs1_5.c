/*
 * Copyright (c) 2017, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 *  Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *          Keyon Jie <yang.jie@linux.intel.com>
 */

#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/sha.h>
#include <openssl/objects.h>
#include <stdio.h>
#include <errno.h>

#include "config.h"
#include "rimage.h"
#include "css.h"
#include "manifest.h"

#define DEBUG_PKCS	0

static void bytes_swap(uint8_t *ptr, uint32_t size)
{
	uint8_t tmp;
	uint32_t index;

	for (index = 0; index < (size / 2); index++) {
		tmp = ptr[index];
		ptr[index] = ptr[size - 1 - index];
		ptr[size - 1 - index] = tmp;
	}
}

/*
 * RSA signature of manifest. The signature is an PKCS
 * #1-v1_5 of the entire manifest structure, including all
 * extensions, and excluding the last 3 fields of the
 * manifest header (Public Key, Exponent and Signature).
*/

int pkcs_sign(struct image *image, struct fw_image_manifest *man,
	void *ptr1, unsigned int size1, void *ptr2, unsigned int size2)
{
	RSA *priv_rsa = NULL;
	EVP_PKEY *privkey;
	FILE *fp;
	unsigned char digest[SHA256_DIGEST_LENGTH], mod[MAN_RSA_KEY_MODULUS_LEN];
	unsigned int siglen = MAN_RSA_SIGNATURE_LEN;
	char path[256];
	int ret = -EINVAL, i;

#if DEBUG_PKCS
	fprintf(stdout, "offsets 0x%lx size 0x%x offset 0x%lx size 0x%x\n",
		ptr1 - (void *)man, size1, ptr2 - (void *)man, size2);
#endif

	/* create new key */
	privkey = EVP_PKEY_new();
	if (privkey == NULL)
		return -ENOMEM;

	/* load in RSA private key from PEM file */
	if (image->key_name == NULL) {
		sprintf(path, "%s/otc_private_key.pem", PEM_KEY_PREFIX);
		image->key_name = path;
	}

	fprintf(stdout, " pkcs: signing with key %s\n", image->key_name);
	fp = fopen(image->key_name, "r");
	if (fp == NULL) {
		fprintf(stderr, "error: can't open file %s %d\n", path, -errno);
		return -errno;
	}
	PEM_read_PrivateKey(fp, &privkey, NULL, NULL);
	fclose(fp);

	/* validate RSA private key */
	priv_rsa = EVP_PKEY_get1_RSA(privkey);
	if (RSA_check_key(priv_rsa)) {
		fprintf(stdout, " pkcs: RSA private key is valid.\n");
	} else {
		fprintf(stderr, "error: validating RSA private key.\n");
		return -EINVAL;
	}

	/* calculate the digest */
	module_sha256_create(image);
	module_sha256_update(image, ptr1, size1);
	module_sha256_update(image, ptr2, size2);
	module_sha256_complete(image, digest);

	fprintf(stdout, " pkcs: digest for manifest is ");
	for (i = 0; i < SHA256_DIGEST_LENGTH; i++)
		fprintf(stdout, "%02x", digest[i]);
	fprintf(stdout, "\n");

	/* sign the manifest */
	ret = RSA_sign(NID_sha256, digest, SHA256_DIGEST_LENGTH,
		(unsigned char *)man->css.signature,
		&siglen, priv_rsa);
	if (ret < 0)
		fprintf(stderr, "error: failed to sign manifest\n");

	/* copy public key modulus and exponent to manifest */
	BN_bn2bin(priv_rsa->n, mod);
	BN_bn2bin(priv_rsa->e, (unsigned char *)man->css.exponent);

	/* modulus is reveresd  */
	for (i = 0; i < MAN_RSA_KEY_MODULUS_LEN; i++)
		man->css.modulus[i] = mod[MAN_RSA_KEY_MODULUS_LEN - (1 + i)];

	/* signature is reveresd, swap it */
	bytes_swap(man->css.signature, sizeof(man->css.signature));

	EVP_PKEY_free(privkey);
	return ret;
}

int ri_manifest_sign(struct image *image)
{
	struct fw_image_manifest *man = image->fw_image;

	pkcs_sign(image, man, (void *)man + MAN_CSS_HDR_OFFSET,
		sizeof(struct css_header) -
		(MAN_RSA_KEY_MODULUS_LEN + MAN_RSA_KEY_EXPONENT_LEN +
		MAN_RSA_SIGNATURE_LEN),
		(void *)man + MAN_SIG_PKG_OFFSET,
		(man->css.size - man->css.header_len) * sizeof(uint32_t));
	return 0;
}
