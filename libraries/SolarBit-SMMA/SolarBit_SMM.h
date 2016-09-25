// Solar Mining Module Prototype A
// https://www.solarbit.cc

#ifndef SMM_H
#define SMM_H

#define SMM_FIRMWARE_REQUIRED "1.0.0-A"

#include <Arduino.h>

const uint8_t MAGIC[] =  {'S', 'M', 'M', 0}; // Solar Mining Module "SMM"
const uint8_t VERSION[] = {0, 3, 0, 'A'}; // 32 bit of www.semver.org

#define MAX_COINBASE_SIZE 64

extern "C" {
	#include "solarbit_types.h"
	#include "sha256.h"
	#include "xxtea.h"
}

typedef enum {
	SMM_EMULATED,
	SMM_HARDWARE
} smm_mode_t;

typedef enum {
	SMM_NO_SHIELD = 255,
	SMM_IDLE = 0,
	SMM_READY,
	SMM_MINING,
	SMM_DONE,
	SMM_FAIL,
	SMM_ERROR
} smm_status_t;


typedef struct {
	uint32_t height;
	uint8_t coinbase[MAX_COINBASE_SIZE];
	uint8_t coinbase_length;
	uint8_t merkle_path[16][HASH_SIZE]; // TODO: Questionable
	uint8_t merkle_path_length;
	block_t block;
	uint8_t target[HASH_SIZE];
	uint8_t hash[HASH_SIZE];
	uint32_t nonce;
} smm_work_t;


class SMMClass
{
public:
	SMMClass();
	const char* firmwareVersion();
	uint8_t status();

	int report(uint8_t *buf, int size); // TODO: improve

	uint8_t begin(uint8_t *coinbase, size_t len);

	uint8_t init(uint32_t block_height, block_t *block_header, int path_length, uint8_t *path_bytes);
	uint8_t mine();
	uint8_t mine(int cycles);
	void end();

	int encrypt(uint8_t *bytes, int size, int payload_size, uint32_t *key);
	int decrypt(uint8_t *bytes, int size, uint32_t *key);

private:
	smm_mode_t _mode;
	smm_status_t _status;
	smm_work_t _work;
	uint8_t *_merkle_path[HASH_SIZE];

	void dhash(uint8_t *bytes, int size, uint8_t *hash); // make private
	boolean set_target(uint32_t bits, uint8_t *target);
};

extern SMMClass SMM;

#endif /* SMM_H */
