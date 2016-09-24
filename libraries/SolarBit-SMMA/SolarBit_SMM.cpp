#include "SolarBit_SMM.h"

SMMClass::SMMClass() {
	_mode = SMM_RESET_MODE;
	_status = SMM_NO_SHIELD;
	_init = 0;
}

const char* SMMClass::firmwareVersion() {
	return SMM_FIRMWARE_REQUIRED;
}

uint8_t SMMClass::status() {
	return _status;
}

uint8_t SMMClass::begin(uint8_t *coinbase, size_t len) {
	return _mode;
}

void SMMClass::setBlock(uint32_t block_height, block_t *block_header, uint8_t **merkle_path, int path_length) {
	//
}

uint8_t SMMClass::mine() {
	return _status;
}

void SMMClass::end() {
	// TODO: idle the ASIC
}

// make private
void SMMClass::doHash(block_t *block, uint8_t *hash) {
	uint8_t temp[HASH_SIZE];
	sha256_digest(block->bytes, sizeof(block_t), temp);
	uint8_t temp2[HASH_SIZE];
	sha256_digest(temp, HASH_SIZE, temp2);
	for (int i = 0, j = 31; i < HASH_SIZE; i++, j--) {
		hash[i] = temp2[j];
	}
}


int SMMClass::encrypt(uint8_t *bytes, int size, int payload_size, uint32_t *key) {
	if (payload_size == 0) return 0;
	int pkcs7pad = 4 - payload_size % 4;
	int encrypted_size = payload_size + pkcs7pad;
	if (encrypted_size > size) return 0;
	int n = encrypted_size / 4;
	memset(&bytes[payload_size], pkcs7pad, pkcs7pad);
	xxtea_encode((uint32_t *) bytes, n, key);
	return encrypted_size;
}


int SMMClass::decrypt(uint8_t *bytes, int size, uint32_t *key) {
	if (size == 0) return 0;
	if (size < 0 || size % 4 != 0) return -1;
	int n = size / 4;
	xxtea_decode((uint32_t *) bytes, n, key);
	int pkcs7pad = bytes[size - 1];
	return size - pkcs7pad;
}


SMMClass SMM;
