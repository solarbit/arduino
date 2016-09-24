// Solar Mining Module Prototype A
// https://www.solarbit.cc

#ifndef SMM_H
#define SMM_H

#define SMM_FIRMWARE_REQUIRED "1.0.0-A"

#include <Arduino.h>

const uint8_t MAGIC[] =  {'S', 'M', 'M', 0}; // Solar Mining Module "SMM"
const uint8_t VERSION[] = {0, 3, 0, 'A'}; // 32 bit of www.semver.org

extern "C" {
	#include "solarbit_types.h"
	#include "sha256.h"
	#include "xxtea.h"
}

typedef enum {
	SMM_NO_SHIELD = 255,
	SMM_IDLE = 0,
	SMM_HASHING,
	SMM_DONE,
	SMM_FAIL
} smm_status_t;

typedef enum {
	SMM_RESET_MODE = 0,
	SMM_READY_MODE
} smm_mode_t;

typedef struct {
	uint32_t height;
	uint32_t start;
	block_t block;
} smm_work_t;

class SMMClass
{
public:
	SMMClass();
	const char* firmwareVersion();
	uint8_t status();
	uint8_t begin(uint8_t *coinbase, size_t len);
	void setBlock(uint32_t block_height, block_t *block_header, uint8_t **merkle_path, int path_length);
	uint8_t mine();
	void end();

	// Temp
	int encrypt(uint8_t *bytes, int size, int payload_size, uint32_t *key);
	int decrypt(uint8_t *bytes, int size, uint32_t *key);
	void doHash(block_t *block, uint8_t *hash);

private:
	int _init;
	smm_status_t _status;
	smm_mode_t _mode;
	char _version[9];
	uint32_t _nonce;
	uint32_t _nonce2;
};

extern SMMClass SMM;

#endif /* SMM_H */
