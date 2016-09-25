// Solar Mining Module Prototype A
// https://www.solarbit.cc
// NOTE: Run solarbit_setup first to configure EEPROM parameters!

#include <EEPROM.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <SolarBit_SMM.h>

typedef struct {
	boolean tethered;
	boolean paused;
	unsigned long boot_time;
} status_t;

status_t status;
config_t config;
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
		int status = SMM.status();
		println("SolarBit Mining Module Ready");
		print("SMM Shield Status: ");
		println(status);
		println();
		print("SMM Status:\n ");
		println(get_status_type(status));
		send_message(HELO);
		delay(500);
	}
	status.paused = true;
}



void loop() {
	if (WiFi.status() == WL_CONNECTED) {
		check_pool();
		int status = SMM.mine();
		if (status == SMM_DONE) {
			print("SMM Status:\n ");
			println(get_status_type(status));
			send_message(DONE); // TODO: add payload
			SMM.end();
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

// TODO: Maybe a shield LED?
void do_indicator() {
	// SPI used for WiFi takes away LED pin 13
	return;
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

/* DO NOT DELETE - MOVE!
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
*/

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
		case POOL: {
				uint8_t *coinbase = (uint8_t *)&packet.message.payload;
				uint8_t length = packet.message.header.payload_size;

				println("Received COINBASE from Pool");
				print_hex(coinbase, length);

				int status = SMM.begin(coinbase, length);
				if (status == SMM_READY) {
					send_message(OKAY);
				} else {
					send_message(NACK, (uint8_t *)&packet.message.header.sync, 4);
				}

				print("SMM Status:\n ");
				println(get_status_type(status));
			}
			break;
		case STAT:
			block_t report;
			SMM.report(report.bytes, sizeof(block_t));
			send_message(INFO, report.bytes, sizeof(block_t));
			break;
		case MINE: {
				int offset = sizeof(message_header_t);
				uint32_t *block_height = (uint32_t *)&packet.bytes[offset];
				offset += sizeof(uint32_t);
				block_t *block = (block_t *)&packet.bytes[offset];
				offset += sizeof(block_t);
				uint8_t *path_length = (uint8_t *)&packet.bytes[offset];
				offset += sizeof(uint8_t);
				uint8_t *merkle_path_bytes = &packet.bytes[offset];
				int status = SMM.init(*block_height, block, *path_length, merkle_path_bytes);
				if (status != SMM_MINING) {
					send_message(NACK);
				}

				println("Mining Payload");
				print(" height=");
				print(*block_height);
				print(" version=");
				print(block->header.version);
				print(" pathlength=");
				println(*path_length);
				print_hex(merkle_path_bytes, *path_length * HASH_SIZE);
				print("SMM Status:\n ");
				println(get_status_type(status));
			}
			break;
		case WAIT:
			status.paused = true;
			// TODO - maybe? send_message(OKAY);
			break;
		case TEST:
			send_message(NACK);
			break;
		case ERROR:
			send_message(NACK);
			break;
		default:
			break;
	}
}


boolean send_message(int type) {
	return send_message(type, NULL, 0);
}


boolean send_message(int type, uint8_t *payload, int payload_size) {
	memset(packet.bytes, 0, sizeof(packet_t));
	const char* typestr = get_message_type(type);
	memcpy(packet.message.header.magic, MAGIC, 4);
	memcpy(packet.message.header.version, VERSION, 4);
	packet.message.header.sync = millis();
	memcpy(packet.message.header.message_type, typestr, 4);
	memcpy(packet.message.payload, payload, payload_size);
	if (payload_size > 0) {
		packet.message.header.payload_size =
			SMM.encrypt(packet.message.payload, sizeof(packet.message.payload), payload_size, config.param.key);
	}

	print("SEND '");
	print(typestr);
	print("' payload=");
	print(payload_size);
	print(" pad=");
	println(payload_size > 0 ? 4 - (payload_size % 4) : 0);
	print_hex(packet.bytes, sizeof(message_header_t) + payload_size);

	IPAddress remote = config.param.server;
	udp.beginPacket(remote, config.param.port);
	int packet_size = sizeof(message_header_t) + packet.message.header.payload_size;
	udp.write(packet.bytes, packet_size);
	return udp.endPacket() != 0;
}


int receive_message() {
	memset(packet.bytes, 0, sizeof(packet_t));
	int packet_size = udp.parsePacket();
	if (packet_size == 0) return NONE;
	if (packet_size < (int) sizeof(message_header_t) || packet_size > MAX_UDP_PAYLOAD) {
		print("ERROR: packet size=");
		println(packet_size);
		udp.flush();
		return ERROR;
	}
	udp.read(packet.bytes, packet_size);
	int type = get_message_type(packet.message.header.message_type);
	packet.message.header.payload_size =
		SMM.decrypt(packet.message.payload, packet.message.header.payload_size, config.param.key);

	print("RECV '");
	print(get_message_type(type));
	print("' size=");
	println(packet.message.header.payload_size);
	print_hex(packet.bytes, sizeof(message_header_t) + packet.message.header.payload_size);

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


const char *get_status_type(int type) {
	switch(type) {
		case SMM_NO_SHIELD: return "SMM_NO_SHIELD";
		case SMM_IDLE: return "SMM_IDLE";
		case SMM_READY: return "SMM_READY";
		case SMM_MINING: return "SMM_MINING";
		case SMM_DONE: return "SMM_DONE";
		case SMM_FAIL: return "SMM_FAIL";
		case SMM_ERROR: return "SMM_ERROR";
		default: return "UNKNOWN";
	}
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
