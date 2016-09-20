// Solar Mining Module Prototype A
// https://www.solarbit.cc
// NOTE: Run solarbit_setup first to configure EEPROM parameters!

#include <EEPROM.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <SolarBit.h>

typedef struct {
	boolean valid;
	boolean mined;
	boolean wait;
	boolean spare; // spare flag
	uint8_t target[HASH_SIZE];
	uint8_t current_hash[HASH_SIZE];
	uint8_t best_hash[HASH_SIZE];
	uint32_t start_nonce;
	uint32_t best_nonce;
	unsigned long start_time;
	unsigned long cumulative_time;
} status_t;

config_t config;
status_t status;
block_t block;
uint8_t packet[MAX_UDP_PAYLOAD];
uint8_t coinbase[1024]; // TODO
WiFiUDP udp;


void setup() {
	Serial.begin(9600);
	while (! Serial);
	boolean success;
	success = load_configuration_from_eeprom();
	if (success) {
		success = connect_to_wifi();
	}
	if (success) {
		udp.begin(config.param.port);
		Serial.println("SMM Ready");
		send_message("HELO", 0, NULL);
		delay(500);
	}
	status.wait = true;
}


void loop() {
	if (WiFi.status() == WL_CONNECTED) {
		check_pool();
		boolean success = mine();
		if (success) {
			send_message("DONE", 4, (uint8_t *) &block.header.nonce);
		}
	} else {
		delay(10000);
	}
}


void do_indicator() {
	// SPI used for WiFi takes awaly LED pin 13
	return;
}


boolean mine() {
	if (status.valid && !status.wait && !status.mined) {
		do_hash(&block, status.current_hash);
		int compare = memcmp(status.current_hash, status.target, HASH_SIZE);
		if (compare <= 0) {
			status.mined = true;
			status.wait = true;
			status.cumulative_time += millis() - status.start_time;
			memcpy(status.best_hash, status.current_hash, HASH_SIZE);
			status.best_nonce = block.header.nonce;
			return true;
		} else {
			int compare2 = memcmp(status.current_hash, status.best_hash, HASH_SIZE);
			if (compare2 <= 0) {
				memcpy(status.best_hash, status.current_hash, HASH_SIZE);
				status.best_nonce = block.header.nonce;
				send_best_result(); // TEMPORARY - REMOVE LATER
			}
			block.header.nonce += 1;
		}
	}
	return false;
}


boolean load_configuration_from_eeprom() {
	for (unsigned int i = 0; i < sizeof(config_t); i++) {
		config.bytes[i] = EEPROM.read(i);
	}
	return is_configuration_valid();
}


boolean is_configuration_valid() {
	return memcmp((const uint8_t*)config.param.magic, MAGIC, 4) == 0
		&& memcmp((const uint8_t*)config.param.trailer, MAGIC, 4) == 0;
}

boolean connect_to_wifi() {
	int status = WiFi.status();
	int tries = 3;
	while (status != WL_CONNECTED && tries > 0) {
		status = WiFi.begin((char *)config.param.ssid, (char *)config.param.password);
		if (status != WL_CONNECTED) {
			tries--;
			delay(5000);
		}
	}
	return (WiFi.status() == WL_CONNECTED);
}


boolean connect_to_mining_pool() {
	udp.begin(config.param.port);
	int tries = 3;
	boolean success = false;
	while (tries > 0 && !success) {
		send_message("HELO", 0, NULL);
		tries--;
		delay(1000);
		success = receive_packet(packet, MAX_UDP_PAYLOAD);
	}
	return success > 0;
}


void load_block(uint8_t *bytes) {
	status.valid = false;
	for (int i = 0; i < BLOCK_HEADER_SIZE; i++) {
		block.bytes[i] = bytes[i];
	}
	if (block.header.version > 0) {
		status.valid = true;
	}
	status.valid = set_target(block.header.bits, status.target);
	memcpy(status.current_hash, MAX_HASH, HASH_SIZE);
	memcpy(status.best_hash, MAX_HASH, HASH_SIZE);
	status.start_time = 0;
	status.cumulative_time = 0;
	status.start_nonce = block.header.nonce;
	status.mined = false;
	status.wait = true;
}


boolean set_target(uint32_t bits, uint8_t *target) {
	memset(target, 0, HASH_SIZE);
	uint32_t mantissa = bits & 0x00FFFFFF;
	uint8_t exponent = bits >> 24 & 0x1F;
	if (exponent > 3) {
		target[32 - exponent] = mantissa >> 16 & 0xFF;
		target[33 - exponent] = mantissa >> 8 & 0xFF;
		target[34 - exponent] = mantissa & 0xFF;
		return true;
	} else {
		return false;
	}
}


double get_hashrate() {
	double hashes = (block.header.nonce - status.start_nonce) / 1000.0;
	if (status.wait) {
		if (status.cumulative_time > 0) {
			return hashes / (status.cumulative_time / 1000.0);
		} else {
			return 0.0;
		}
	} else {
		return hashes / ((millis() - status.start_time + status.cumulative_time) / 1000.0);
	}
}


void do_hash(block_t *block, uint8_t *hash) {
	uint8_t temp[HASH_SIZE];
	sha256_digest(block->bytes, sizeof(block_t), temp);
	uint8_t temp2[HASH_SIZE];
	sha256_digest(temp, HASH_SIZE, temp2);
	for (int i = 0, j = 31; i < HASH_SIZE; i++, j--) {
		hash[i] = temp2[j];
	}
}


void check_pool() {
	int packet_size = receive_packet(packet, MAX_UDP_PAYLOAD);
	if (packet_size) {
		message_t m;
		int cmd = parse_message_header(packet, packet_size, &m);
		size_t payload_size = packet_size - sizeof(message_t);
		// size_t len = 0;
		switch (cmd) {
			case PING:
				send_message("HELO", 0, NULL);
				break;
			case INFO:
				send_message("NODE", strlen((char *)config.param.address), config.param.address);
				break;
			case POOL:
				send_message("OKAY", 0, NULL);
				break;
			case STAT:
				send_best_result(); // For now
				break;
			case MINE:
				// TODO - NEED COINBASE SPEC, BLOCK HEIGHT, MERKLE PATH...
				send_message("NACK", 0, NULL);
				break;
			case WAIT:
				status.wait = true;
				//send_message("OKAY", 0, NULL);
				break;
			case TEST:
				if (m.header.payload_size >= sizeof(block_t) && sizeof(block_t) <= payload_size)  {
					memcpy(block.bytes, &packet[sizeof(message_t)], sizeof(block_t));
					status.valid = false;
					if (block.header.version > 0) {
						status.valid = true;
					}
					status.valid = set_target(block.header.bits, status.target);
					memcpy(status.current_hash, MAX_HASH, HASH_SIZE);
					memcpy(status.best_hash, MAX_HASH, HASH_SIZE);
					status.start_time = 0;
					status.cumulative_time = 0;
					status.start_nonce = block.header.nonce;
					status.mined = false;
					status.wait = false;
				} else {
					send_message("NACK", 0, NULL);
				}
				break;
			default:
				send_message("NACK", 0, NULL);
				break;
		}
	}
}


// TODO: It's a start...
void send_best_result() {
	send_message("BEST", sizeof(uint32_t), (uint8_t *)&status.best_nonce);
}


boolean send_message(const char *type, int payload_size, uint8_t *payload) {
	int header_size = sizeof(message_t);
	int pkcs7pad = 0;
	if (payload_size > 0) {
		pkcs7pad = 4 - payload_size % 4;
	}
	int n = (payload_size + pkcs7pad) / 4;
	int packet_size = header_size + payload_size + pkcs7pad;

	Serial.println(type); // remove
	Serial.print("size=");
	Serial.print(payload_size); // remove
	Serial.print(" pad=");
	Serial.print(pkcs7pad); // remove
	Serial.print(" n=");
	Serial.println(n);

	memset(packet, 0, MAX_UDP_PAYLOAD);
	message_t m = build_message_header(type, payload_size + pkcs7pad);
	memcpy(packet, m.bytes, header_size);
	if (payload_size > 0) {
		memcpy(&packet[header_size], payload, payload_size);
		memset(&packet[header_size + payload_size], pkcs7pad, pkcs7pad);
		print_hex(packet, packet_size); // remove
		uint32_t *offset = (uint32_t *) &packet[header_size];
		xxtea_encode(offset, n, config.param.key);
	}
	print_hex(packet, packet_size); // remove
	return send_packet(packet, packet_size);
}


void print_hex(uint8_t *buf, int length) {
	for (int i = 0; i < length; i++) {
		if (i > 0 && i % 32 == 0) {
			Serial.println();
		}
		Serial.print(' ');
		int value = buf[i];
		if (value <= 0x0F) {
			Serial.print('0');
		}
		Serial.print(value, HEX);
	}
	Serial.println();
}

message_t build_message_header(const char *type, int payload_size) {
	message_t m;
	memcpy(m.header.magic, MAGIC, 4);
	memcpy(m.header.version, VERSION, 4);
	m.header.sync = millis();
	memcpy(m.header.message_type, type, 4);
	m.header.payload_size = payload_size;
	return m;
}


int parse_message_header(uint8_t *buf, size_t size, message_t *m) {
	if (size >= sizeof(message_t)) {
		memcpy(m->bytes, buf, sizeof(message_t));
		return get_message_type(m->header.message_type);
	} else {
		return -1;
	}
}


const char *get_message_type(int type) {
	switch (type) {
		case HELO: return "HELO";
		case NODE: return "NODE";
		case INFO: return "INFO";
		case DONE: return "DONE";
		case BEST: return "BEST";
		case OKAY: return "OKAY";
		default: return "NACK";
	}
}


int get_message_type(uint8_t *cmd) {
	if (strncmp("PING", (char *)cmd, 4) == 0) return PING;
	if (strncmp("HELO", (char *)cmd, 4) == 0) return HELO;
	if (strncmp("INFO", (char *)cmd, 4) == 0) return INFO;
	if (strncmp("POOL", (char *)cmd, 4) == 0) return POOL;
	if (strncmp("MINE", (char *)cmd, 4) == 0) return MINE;
	if (strncmp("WAIT", (char *)cmd, 4) == 0) return WAIT;
	if (strncmp("STOP", (char *)cmd, 4) == 0) return STOP;
	if (strncmp("STAT", (char *)cmd, 4) == 0) return STAT;
	if (strncmp("TEST", (char *)cmd, 4) == 0) return TEST;
	return NACK;
}


boolean send_packet(uint8_t *bytes, size_t size) {
	IPAddress remote = config.param.server;
	udp.beginPacket(remote, config.param.port);
	udp.write(bytes, size);
	return udp.endPacket() != 0;
}


int receive_packet(uint8_t *buf, int size) {
	int packet_size = udp.parsePacket();
	if (packet_size == 0) {
		return 0;
	}
	if (packet_size > MAX_UDP_PAYLOAD) {
		udp.flush();
		return 0;
	}
	memset(buf, 0, size);
	udp.read(buf, packet_size);
	return packet_size;
}
