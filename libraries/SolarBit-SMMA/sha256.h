/*********************************************************************
* Filename:   sha256.h
* Author:     Brad Conte (brad AT bradconte.com)
* Copyright:
* Disclaimer: This code is presented "as is" without any guarantees.
*********************************************************************/

#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <inttypes.h>

#define SHA256_BLOCK_SIZE 32

typedef struct {
	uint8_t data[64];
	uint32_t datalen;
	unsigned long long bitlen;
	uint32_t state[8];
} SHA256_CTX;

#ifdef __cplusplus
extern "C" {
#endif

void sha256_digest(uint8_t *bytes, size_t len, uint8_t *hash);
void sha256_init(SHA256_CTX *ctx);
void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len);
void sha256_final(SHA256_CTX *ctx, uint8_t hash[]);

#ifdef __cplusplus
}
#endif

#endif   // SHA256_H
