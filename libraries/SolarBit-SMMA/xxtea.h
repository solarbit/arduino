
#ifndef XXTEA_H
#define XXTEA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void xxtea_encode(uint32_t *v, int n, uint32_t const key[4]);
void xxtea_decode(uint32_t *v, int n, uint32_t const key[4]);

#ifdef __cplusplus
}
#endif

#endif // XXTEA_H
