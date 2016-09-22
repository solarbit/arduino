// Solar Mining Module Prototype A
// https://www.solarbit.cc
// NOTE: Run solarbit_setup first to configure EEPROM parameters!

#include <EEPROM.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <SolarBit.h>

#define MAX_COINBASE_SIZE 100

typedef struct {
	boolean tethered;
	boolean valid;
	boolean mined;
	boolean paused;
	uint8_t target[HASH_SIZE];
	uint8_t current_hash[HASH_SIZE];
	uint8_t best_hash[HASH_SIZE];
	uint32_t start_nonce;
	uint32_t best_nonce;
	uint8_t coinbase[MAX_COINBASE_SIZE];
	unsigned long boot_time;
	unsigned long start_time;
	unsigned long cumulative_time;
} status_t;


config_t config;
status_t status;
block_t block;
packet_t packet;
WiFiUDP udp;


void setup() {
	status.tethered = is_tethered();
	boolean success = load_configuration_from_eeprom();
	if (success) {
		success = connect_to_wifi();
	}
	if (success) {
		udp.begin(config.param.port);
		println("SolarBit Mining Module Ready");
		println();
		send_message(HELO);
		delay(500);
	}
	status.paused = true;
}



void loop() {
	if (WiFi.status() == WL_CONNECTED) {
		check_pool();
		boolean success = mine();
		if (success) {
			send_message(DONE, (uint8_t *) &block.header.nonce, 4);
		}
	} else {
		println("WiFi Disconnected");
		do {
			if (! connect_to_wifi()) {
				delay(60000);
			}
		} while (WiFi.status() != WL_CONNECTED);
		println("WiFi Reconnected");
	}
}


void do_indicator() {
	// SPI used for WiFi takes awaly LED pin 13
	return;
}


boolean mine() {
	if (status.valid && !status.paused && !status.mined) {
		do_hash(&block, status.current_hash);
		int compare = memcmp(status.current_hash, status.target, HASH_SIZE);
		if (compare <= 0) {
			status.mined = true;
			status.paused = true;
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
		send_message(HELO);
		tries--;
		delay(1000);
		success = receive_message();
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
	status.paused = true;
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
	if (status.paused) {
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
	int cmd = receive_message();
	switch (cmd) {
		case NONE:
			break;
		case PING:
			send_message(HELO);
			break;
		case SYNC:
			status.boot_time = packet.message.header.sync - millis();
			send_message(NODE, config.param.address, strlen((char *)config.param.address));
			break;
		case POOL:
			if (packet.message.header.payload_size <= MAX_COINBASE_SIZE) {
				memset(status.coinbase, 0, MAX_COINBASE_SIZE);
				memcpy(status.coinbase, packet.message.payload, packet.message.header.payload_size);
				send_message(OKAY);
			} else {
				send_message(NACK, (uint8_t *)&packet.message.header.sync, 4);
			}
			break;
		case STAT:
			send_message(INFO, (uint8_t *)&status, sizeof(status_t));
			break;
		case MINE:
			// TODO - NEED COINBASE SPEC, BLOCK HEIGHT, MERKLE PATH...
			send_message(NACK);
			break;
		case WAIT:
			status.paused = true;
			//send_message("OKAY", 0, NULL);
			break;
		case TEST:
			if (packet.message.header.payload_size >= sizeof(block_t))  {
				memcpy(block.bytes, packet.message.payload, sizeof(block_t));
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
				status.paused = false;
			} else {
				send_message(NACK);
			}
			break;
		case ERROR:
			send_message(NACK);
			break;
		default:
			break;
	}
}


// TODO: It's a start...
void send_best_result() {
	send_message(BEST, (uint8_t *)&status.best_nonce, sizeof(uint32_t));
}


boolean send_message(int type) {
	return send_message(type, NULL, 0);
}

boolean send_message(int type, uint8_t *bytes, int bytes_size) {
	const char* typestr = get_message_type(type);
	int header_size = sizeof(message_header_t);

	int pkcs7pad = 0;
	if (bytes_size > 0) {
		pkcs7pad = 4 - bytes_size % 4;
	}
	int payload_size = bytes_size + pkcs7pad;
	int n = payload_size / 4;
	int packet_size = header_size + payload_size;

	memset(packet.bytes, 0, sizeof(packet_t));
//	message_header_t m = packet2.header;
	memcpy(packet.message.header.magic, MAGIC, 4);
	memcpy(packet.message.header.version, VERSION, 4);
	packet.message.header.sync = millis();
	memcpy(packet.message.header.message_type, typestr, 4);
	packet.message.header.payload_size = payload_size;

	// Remove
	print("SEND '"); print(typestr); print("' size="); print(bytes_size);
	print(" pad="); print(pkcs7pad); print(" n="); println(n);

	if (payload_size > 0) {
		memcpy(packet.message.payload, bytes, bytes_size);
		memset(&packet.message.payload[bytes_size], pkcs7pad, pkcs7pad);
		print_hex(packet.bytes, packet_size);
		xxtea_encode((uint32_t *) packet.message.payload, n, config.param.key);
	} else {
		print_hex(packet.bytes, packet_size);
	}
	return send_packet(packet.bytes, packet_size);
}

boolean send_packet(uint8_t *bytes, size_t size) {
	IPAddress remote = config.param.server;
	udp.beginPacket(remote, config.param.port);
	udp.write(bytes, size);
	return udp.endPacket() != 0;
}



int receive_message() {
	int header_size = sizeof(message_header_t);
	memset(packet.bytes, 0, sizeof(packet_t));
	int packet_size = receive_packet(packet.bytes, sizeof(packet_t));
	if (packet_size <= 0) return NONE;
	if (packet_size < (int) sizeof(message_header_t)) {
		print("ERROR: packet size=");
		println(packet_size);
		print_hex(packet.bytes, packet_size);
		return ERROR;
	}

	int type = get_message_type(packet.message.header.message_type);
	int payload_size = packet.message.header.payload_size;
	int pkcs7pad = 0, n = 0;
	if (payload_size > 0) {
		int n = payload_size / 4;
		xxtea_decode((uint32_t *) packet.message.payload, n, config.param.key);
		pkcs7pad = packet.message.payload[packet_size - 1];
		packet.message.header.payload_size = packet_size - pkcs7pad - header_size;
	}
	print("RECV '");
	print(get_message_type(type));
	print("' size=");
	print(packet.message.header.payload_size);
	print(" pad=");
	print(pkcs7pad);
	print(" n=");
	println(n);
	print_hex(packet.bytes, packet_size); // remove
	return type;
}


const char *get_message_type(int type) {
	switch (type) {
		case PING: return "PING";
		case HELO: return "HELO";
		case SYNC: return "SYNC";
		case NODE: return "NODE";
		case POOL: return "POOL";
		case MINE: return "MINE";
		case DONE: return "DONE";
		case WAIT: return "WAIT";
		case STAT: return "STAT";

		case TEST: return "TEST";
		case INFO: return "INFO";
		case BEST: return "BEST";
		case STOP: return "STOP";
		case OKAY: return "OKAY";

		default: return "NACK";
	}
}


int get_message_type(uint8_t *cmd) {
	if (strncmp("PING", (char *)cmd, 4) == 0) return PING;
	if (strncmp("HELO", (char *)cmd, 4) == 0) return HELO;
	if (strncmp("SYNC", (char *)cmd, 4) == 0) return SYNC;
	if (strncmp("NODE", (char *)cmd, 4) == 0) return NODE;
	if (strncmp("POOL", (char *)cmd, 4) == 0) return POOL;
	if (strncmp("MINE", (char *)cmd, 4) == 0) return MINE;
	if (strncmp("DONE", (char *)cmd, 4) == 0) return DONE;
	if (strncmp("WAIT", (char *)cmd, 4) == 0) return WAIT;
	if (strncmp("STAT", (char *)cmd, 4) == 0) return STAT;

	if (strncmp("OKAY", (char *)cmd, 4) == 0) return OKAY;
	if (strncmp("INFO", (char *)cmd, 4) == 0) return INFO;
	if (strncmp("STOP", (char *)cmd, 4) == 0) return STOP;
	if (strncmp("TEST", (char *)cmd, 4) == 0) return TEST;

	return NACK;
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

// CONDITIONAL PRINT

boolean is_tethered() {
	Serial.begin(9600);
	int tries = 5;
	while (tries > 0) {
		if (Serial) {
			return true;
		} else {
			delay(1000);
			tries--;
		}
	}
	return false;
}

void print(int i) {
	if (!status.tethered) return;
	Serial.print(i);
}

void print(const char *message) {
	if (!status.tethered) return;
	Serial.print(message);
}

void println() {
	if (!status.tethered) return;
	Serial.println();
}

void println(int i) {
	if (!status.tethered) return;
	Serial.println(i);
}

void println(const char *message) {
	if (!status.tethered) return;
	Serial.println(message);
}

void print_hex(uint8_t *buf, int length) {
	if (!status.tethered) return;
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
