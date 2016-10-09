// Solar Mining Module Prototype A
// https://www.solarbit.cc

#ifndef SOLARBIT_H
#define SOLARBIT_H

#include <Arduino.h>

#define UDP_PORT 21314 // UTF-8 "SB" as a 16-bit NBO integer
#define MAX_UDP_PAYLOAD 1472 // Reduce packet fragmentation: 1500 MTU - 20 IP hdr - 8 UDP hdr = 1472 bytes

#define HASH_SIZE 32 // SHA-256
#define BLOCK_HEADER_SIZE 80 // Bitcoin Block Header Size in bytes
#define ERROR -1

const uint8_t MAX_HASH[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
}; // Almost completely guaranteed to be beaten on a first nonce attempt


// SolarBit Mining Protocol Message Types
enum MessageType {
	NACK = 255,
	NONE = 0,
	PING, HELO, SYNC,
	NODE, POOL, OKAY,
	MINE, DONE, WAIT,
	STAT, INFO, WARN,
};

// SolarBit Mining Protocol Message Data Structures
typedef struct {
	uint8_t magic[4];
	uint8_t version[4];
	uint32_t sync;
	uint8_t message_type[4];
	uint32_t payload_size;
} message_header_t;

typedef struct {
	message_header_t header;
	uint8_t payload[MAX_UDP_PAYLOAD - sizeof(message_header_t)];
} message_t;

typedef union {
	message_t message;
	uint8_t bytes[sizeof(message_t)];
} packet_t;


// SolarBit SMM EEPROM Configuration Data Structure
typedef struct {
	uint8_t magic[4];
	uint8_t version[4];
	uint8_t ssid[32];
	uint8_t password[64];
	uint8_t server[4];
	uint16_t port;
	uint8_t address[40];
	uint32_t key[4];
	uint8_t trailer[4];
} eeprom_config_t;

typedef union {
	eeprom_config_t param;
	uint8_t bytes[sizeof(eeprom_config_t)];
} config_t;


// Bitcoin Block Header Data Structure
typedef struct {
	uint32_t version;
	uint8_t previous_block[HASH_SIZE];
	uint8_t merkle_root[HASH_SIZE];
	uint32_t timestamp;
	uint32_t bits;
	uint32_t nonce;
} block_header_t;

typedef union {
	block_header_t header;
	uint8_t bytes[sizeof(block_header_t)];
} block_t;


// Mining Report
typedef struct {
	uint8_t mode;
	uint8_t status;
	uint8_t tethered;
	uint8_t paused;
	uint32_t height;
	uint32_t nonce;
	uint32_t nonce2;
	uint8_t best_hash[HASH_SIZE];
	unsigned long hash_time;
	double hash_rate;
} report_value_t;

typedef union {
	report_value_t value;
	uint8_t bytes[sizeof(report_value_t)];
} report_t;


#endif // SOLARBIT_H
