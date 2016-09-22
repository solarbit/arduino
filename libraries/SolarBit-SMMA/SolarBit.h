// Solar Mining Module Prototype A
// https://www.solarbit.cc

#include <Arduino.h>
#include "sha256.h"
#include "xxtea.h"

uint8_t MAGIC[] =  {'S', 'M', 'M', 0}; // Solar Mining Module "SMM"
uint8_t VERSION[] = {0, 3, 0, 'A'}; // 32 bit of www.semver.org

#define UDP_PORT 21314 // "SB" as a 16-bit big-endian integer
#define MAX_UDP_PAYLOAD 1472 // Reduce packet fragmentation: 1500 MTU - 20 IP hdr - 8 UDP hdr = 1472 bytes

// #define MESSAGE_HEADER_SIZE 20
#define MAX_PAYLOAD_SIZE 1452

#define HASH_SIZE 32 // SHA-256
#define BLOCK_HEADER_SIZE 80 // Bitcoin Block Header Size in bytes
#define ERROR -1

uint8_t const MAX_HASH[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
}; // Almost completely guaranteed to be beaten on a first nonce attempt


// SolarBit Mining Protocol Message Types
enum MessageTypes {
	NONE = 0,
	HELO,
	NODE,
	POOL,
	MINE,
	DONE,
	WAIT,
	STAT,
	WARN,
	// Maybes...
	SYNC,
	INFO,
	STOP,
	OKAY,
	BEST,
	TEST,
	POST,
	NACK,
	PING
};

// SolarBit Mining Protocol Message Header Data Structure
typedef struct {
	uint8_t magic[4];
	uint8_t version[4];
	uint32_t sync;
	uint8_t message_type[4];
	uint32_t payload_size;
} message_header_t;

/*
typedef union {
	message_header_t header;
	uint8_t bytes[sizeof(message_header_t)];
} message_t;
*/

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
	uint32_t time;
	uint32_t bits;
	uint32_t nonce;
} block_header_t;

typedef union {
	block_header_t header;
	uint8_t bytes[sizeof(block_header_t)];
} block_t;
