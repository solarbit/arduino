#include "SolarBit_SMM.h"


SMMClass::SMMClass() {
	_mode = SMM_EMULATED;
	_status = SMM_IDLE;
}

const char* SMMClass::firmwareVersion() {
	return SMM_FIRMWARE_REQUIRED;
}

uint8_t SMMClass::status() {
	return _status;
}

uint8_t SMMClass::begin(uint8_t *coinbase, size_t len) {
	if (len > MAX_COINBASE_SIZE) {
		return SMM_ERROR;
	}
	memcpy(_work.coinbase, coinbase, len);
	_work.coinbase_length = len;
	_status = SMM_READY;
	return _status;
}

uint8_t SMMClass::init(uint32_t block_height, block_t *block_header, int path_length, uint8_t *path_bytes) {
	if (block_height != (block_height & 0x00FFFFFF)) {
		_status = SMM_ERROR;
		return _status;
	}
	_work.height = block_height;
	// update coinbase
	uint8_t prefix[] = {
		0x03,
		(uint8_t)(block_height & 0xFF),
		(uint8_t)(block_height >> 8 & 0xFF),
		(uint8_t)(block_height >> 16 & 0xFF)
	};
	memcpy(_work.coinbase, prefix, sizeof(prefix));

	// set the merkle path for the block
	_work.merkle_path_length = path_length;
	for (int i = 0, j = 0; i < path_length; i++, j += HASH_SIZE) {
		memcpy(_work.merkle_path[i], &path_bytes[j + i], HASH_SIZE);
	}

	// load block
	memcpy(_work.block.bytes, block_header, sizeof(block_t));

	// set the initial merkle_root
	uint8_t hash[HASH_SIZE];
	uint8_t next[HASH_SIZE * 2];
	dhash(_work.coinbase, _work.coinbase_length, hash);
	for (int i = 0; i < _work.merkle_path_length; i++) {
		memcpy(next, hash, HASH_SIZE);
		memcpy(&next[HASH_SIZE], _work.merkle_path[i], HASH_SIZE);
		dhash(next, sizeof(next), hash);
	}
	memcpy(_work.block.header.merkle_root, hash, HASH_SIZE);

	// TEMP RESET  --- REMOVE
	memcpy(_work.block.bytes, block_header, sizeof(block_t));

	// set the target
	boolean success = set_target(_work.block.header.bits, _work.target);
	_status = success ? SMM_MINING : SMM_ERROR;
	return _status;
}

uint8_t SMMClass::mine() {
	return mine(1024); // 1KH chunk
}

uint8_t SMMClass::mine(int cycles) {
	if (_status != SMM_MINING) return _status;
	for (int i = 0; i < cycles; i++) {
		uint8_t hash[HASH_SIZE];
		dhash(_work.block.bytes, sizeof(block_t), hash);
		boolean mined = (memcmp(hash, _work.target, HASH_SIZE) <= 0);
		if (mined) {
			memcpy(_work.hash, hash, HASH_SIZE);
			_work.nonce = _work.block.header.nonce;
			_status = SMM_DONE;
		} else {
			int bested = (memcmp(hash, _work.hash, HASH_SIZE) <= 0);
			if (bested) {
				memcpy(_work.hash, hash, HASH_SIZE);
				_work.nonce = _work.block.header.nonce;
			}
			if (_work.block.header.nonce == UINT32_MAX) {
				_status = SMM_FAIL;
			} else {
				_work.block.header.nonce += 1;
			}
		}
	}
	return _status;
}

void SMMClass::end() {
	_status = SMM_IDLE;
}


int SMMClass::report(uint8_t *buf, int size) {
	if (size >= (int)sizeof(block_t)) {
		memcpy(buf, _work.block.bytes, sizeof(block_t));
		return sizeof(block_t);
	}
	return 0;
}


boolean SMMClass::set_target(uint32_t bits, uint8_t *target) {
	uint32_t mantissa = bits & 0x00FFFFFF;
	uint8_t exponent = bits >> 24 & 0x1F;
	if (exponent <= 3) {
		return false;
	}
	memset(target, 0, HASH_SIZE);
	target[32 - exponent] = mantissa >> 16 & 0xFF;
	target[33 - exponent] = mantissa >> 8 & 0xFF;
	target[34 - exponent] = mantissa & 0xFF;
	return true;
}

void SMMClass::dhash(uint8_t *input, int size, uint8_t *hash) {
	uint8_t temp[HASH_SIZE];
	sha256_digest(input, size, temp);
	uint8_t temp2[HASH_SIZE];
	sha256_digest(temp, HASH_SIZE, temp2);
	for (int i = 0, j = 31; i < HASH_SIZE; i++, j--) {
		hash[i] = temp2[j];
	}
}


int SMMClass::encrypt(uint8_t *bytes, int size, int payload_size, uint32_t *key) {
	if (payload_size == 0) {
		return 0;
	}
	int pkcs7pad = 4 - payload_size % 4;
	int encrypted_size = payload_size + pkcs7pad;
	if (encrypted_size > size) {
		return payload_size;
	}
	int n = encrypted_size / 4;
	memset(&bytes[payload_size], pkcs7pad, pkcs7pad);
	xxtea_encode((uint32_t *) bytes, n, key);
	return encrypted_size;
}

int SMMClass::decrypt(uint8_t *bytes, int size, uint32_t *key) {
	if (size <= 0 || size % 4 != 0) {
		return size;
	}
	int n = size / 4;
	xxtea_decode((uint32_t *) bytes, n, key);
	int pkcs7pad = bytes[size - 1];
	return size - pkcs7pad;
}


SMMClass SMM;
