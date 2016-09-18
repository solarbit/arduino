// Solar Mining Module Prototype A
// https://www.solarbit.cc

#include <Arduino.h>

uint8_t MAGIC[] =  {'S', 'M', 'M', 0}; // Solar Mining Module "SMM"
uint8_t VERSION[] = {0, 1, 0, 'A'}; // 32 bit of www.semver.org

#define UDP_PORT 21314 // "SB" as 16-bit int in NBO
#define MAX_UDP_PAYLOAD 1472 // 1500 MTU - 20 IP hdr - 8 UDP hdr = 1472 bytes
#define HASH_SIZE 32 // SHA-256
#define BLOCK_HEADER_SIZE 80 // Bitcoin Block Header Size

uint8_t const MAX_HASH[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
}; // Almost completely guaranteed to be beaten on a first nonce attempt

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


// SolarBit Mining Protocol Message Header Data Structure
typedef struct {
	uint8_t magic[4];
	uint8_t version[4];
	uint32_t nonce;
	uint8_t message_type[4];
	uint32_t payload_size;
} message_header_t;

typedef union {
	message_header_t header;
	uint8_t bytes[sizeof(message_header_t)];
} message_t;


// SolarBit SMM EEPROM Configuration Data Structure
typedef struct {
	uint8_t magic[4];
	uint8_t version[4];
	uint8_t ssid[32];
	uint8_t password[64];
	uint8_t server[4];
	uint16_t port;
	uint8_t address[40];
	uint8_t status;
} net_config_t;

typedef union {
	net_config_t net;
	uint8_t bytes[sizeof(net_config_t)];
} config_t;
