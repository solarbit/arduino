#include <EEPROM.h>
#include <SPI.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <sha256.h>
#include "solarbit.h"

enum MessageTypes {
	HELO = 0,
	STAT,
	INFO,
	WAIT,
	MINE,
	DONE,
	BEST,
	TEST
};

typedef struct {
	uint8_t target[HASH_SIZE];
	uint8_t current_hash[HASH_SIZE];
	uint8_t best_hash[HASH_SIZE];
	unsigned long start_time;
	unsigned long cumulative_time;
	uint32_t start_nonce;
	boolean valid;
	boolean mined;
	boolean paused;
} status_t;

config_t config;
status_t status;
block_t block;

WiFiUDP udp;


void setup() {
	Serial.begin(9600);
	while (!Serial);
	boolean success;
	success = load_configuration();
	if (success) {
		config.net.status = WL_IDLE_STATUS;
		success = connect_to_wifi();
	}
	if (success) {
		Serial.print("  Pool Server");
		success = connect_to_pool();
	}
	if (success) {
		Serial.println(": ONLINE");
	} else {
		Serial.println(": OFFLINE");
	}
	status.paused = true;
	Serial.println("\nSolarbit Miner Ready");
}


void loop() {
	if (config.net.status == WL_CONNECTED) {
		do_server_command();
		if (status.valid && !status.paused && !status.mined) {
			do_hash(&block, status.current_hash);
			int compare = memcmp(status.current_hash, status.target, HASH_SIZE);
			if (compare <= 0) {
				status.mined = true;
				status.paused = true;
				status.cumulative_time += millis() - status.start_time;
				for (int i = 0; i < HASH_SIZE; i++) {
					status.best_hash[i] = status.current_hash[i];
				}
				print_status();
			} else {
				int compare2 = memcmp(status.current_hash, status.best_hash, HASH_SIZE);
				if (compare2 <= 0) {
					for (int i = 0; i < HASH_SIZE; i++) {
						status.best_hash[i] = status.current_hash[i];
					}
					print_update();
				}
				block.header.nonce += 1;
			}
		}
	} else {
		delay(10000);
	}
}


boolean load_configuration() {
	for (unsigned int i = 0; i < sizeof(config_t); i++) {
		config.bytes[i] = EEPROM.read(i);
	}
	return memcmp((const uint8_t*)config.net.magic, MAGIC, 4) == 0;
}


boolean connect_to_wifi() {
	int tries = 3;
	while (config.net.status != WL_CONNECTED && tries > 0) {
		config.net.status = WiFi.begin((char *)config.net.ssid, (char *)config.net.password);
		if (config.net.status != WL_CONNECTED) {
			tries--;
			delay(5000);
		}
	}
	if (config.net.status == WL_CONNECTED) {
		Serial.println("CONNECTED");
		Serial.print("  SSID: ");
		Serial.println(WiFi.SSID());
		Serial.print("  IP Address: ");
		print_address(WiFi.localIP());
		Serial.println();
		Serial.print("  Signal strength (RSSI): ");
		Serial.print(WiFi.RSSI());
		Serial.println("dB");
		return true;
	} else {
		Serial.print("Failed to connect to ");
		Serial.println((char *)config.net.ssid);
		return false;
	}
}


boolean connect_to_pool() {
	udp.begin(config.net.port);
	int tries = 3;
	boolean success = false;
	while (tries > 0 && !success) {
		send_ping();
		tries--;
		delay(1000);
		success = receive_ping();
	}
	return success;
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


void do_server_command() {
	int packet_size = udp.parsePacket();
	if (packet_size) {
		int cmd = -1;
		message_t m;
		uint8_t buf[packet_size];
		udp.read(buf, packet_size);
		IPAddress remote = udp.remoteIP();
		print_address(remote);
		Serial.print(" [");
		Serial.print(packet_size);
		Serial.print("] ");
		print_hex(buf, packet_size);
		Serial.println();
		cmd = parse_message(buf, packet_size, &m);
		switch (cmd) {
			case HELO:
				print_configuration();
				break;
			case WAIT:
				status.paused = true;
			case MINE: // TODO
			case TEST:
				Serial.println("TEST");
				if (m.header.size >= sizeof(block_t) && sizeof(block_t) <= packet_size - sizeof(message_t))  {
					memcpy(block.bytes, &buf[sizeof(message_t)], sizeof(block_t));
					print_block();
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
					Serial.print("Size Error:");
					Serial.println(m.header.size);
				}
				break;
			case STAT:
				print_status();
				break;
			default:
				Serial.println(cmd);
				break;
		}
	}
 	//
	char ch = 0;
	if (Serial.available()) {
		ch = Serial.read();
	}
    if (ch) {
      switch (ch) {
        case 'm':
        case 'M':
			char buf[161];
			int num;
			num = Serial.readBytes(buf, 160);
			if (num == 160) {
				uint8_t bytes[80];
				from_hex(buf, 160, bytes);
				load_block(bytes);
				if (status.valid) {
					Serial.println("\nMINING BLOCK...");
					print_hex(status.target, HASH_SIZE);
					Serial.println(":target");
					status.paused = false;
				} else {
					Serial.println("INVALID BLOCK");
				}
				print_block();
			} else {
				Serial.print("ERROR: ");
				Serial.print(num);
				Serial.println(" bytes received\n");
			}
			break;
        case 's':
        case 'S':
			print_status();
    		break;
		case 'p':
		case 'P':
			if (status.valid && !status.mined) {
				status.paused = !status.paused;
				if (status.paused) {
					status.cumulative_time += millis() - status.start_time;
					Serial.println("PAUSED");
				} else {
					Serial.println("RESUMING");
					status.start_time = millis();
				}
			} else {
				Serial.println("WAITING");
			}
			break;
        default:
			Serial.print("UNKNOWN COMMAND '");
        	Serial.print(ch);
        	Serial.println("'");
        	break;
      };
    }
}

int parse_message(uint8_t *buf, size_t size, message_t *m) {
	if (size >= sizeof(message_t)) {
		memcpy(m->bytes, buf, sizeof(message_t));
		return get_command(m->header.command);
	} else {
		Serial.println("Message Parse Failed");
		return -1;
	}
}

int get_command(uint8_t *cmd) {
	if (strncmp("HELO", (char *)cmd, 4) == 0) return HELO;
	if (strncmp("STAT", (char *)cmd, 4) == 0) return STAT;
	if (strncmp("MINE", (char *)cmd, 4) == 0) return MINE;
	if (strncmp("BEST", (char *)cmd, 4) == 0) return BEST;
	if (strncmp("TEST", (char *)cmd, 4) == 0) return TEST;
	Serial.println("Command Parse Failed");
	return -1;
}

boolean notify_server(block_t *block) {
	return false;
}


void print_block() {
	Serial.print("version:");
	Serial.println(block.header.version);
	Serial.print("previous:");
	print_hex(block.header.previous_block, HASH_SIZE);
	Serial.print("\nmerkle root:");
	print_hex(block.header.merkle_root, HASH_SIZE);
	Serial.print("\ntimestamp:");
	Serial.println(block.header.time);
	Serial.print("bits:");
	Serial.println(block.header.bits);
	Serial.print("starting nonce:");
	Serial.println(block.header.nonce);
	Serial.println();
}


void print_update() {
	print_hex(status.best_hash, HASH_SIZE);
	Serial.print(":");
	Serial.println(block.header.nonce);
}


void print_status() {
	Serial.print("\nSTATUS: ");
	if (status.mined) {
		Serial.println("MINED");
	} else if (!status.paused) {
		Serial.println("MINING");
	} else if (status.valid) {
		Serial.println("PAUSED");
	} else {
		Serial.println("WAITING");
	}
	unsigned long time = status.cumulative_time;
	if (!status.paused && !status.mined) {
		time += millis() - status.start_time;
	}
	print_hex(status.target, HASH_SIZE);
	Serial.println(":target");
	print_hex(status.best_hash, HASH_SIZE);
	Serial.println(":best");
	print_hex(status.current_hash, HASH_SIZE);
	Serial.print(":");
	Serial.println(block.header.nonce);
	Serial.print(time);
	Serial.println(":ms");
	Serial.print(get_hashrate());
	Serial.println(":KH/s\n");
}

void print_address(IPAddress addr) {
	Serial.print(addr[0]);
	Serial.print(".");
	Serial.print(addr[1]);
	Serial.print(".");
	Serial.print(addr[2]);
	Serial.print(".");
	Serial.print(addr[3]);

}


void from_hex(char *buf, int size, uint8_t *result) {
	for (int i = 0, j = 0; i < size; i += 2, j++) {
		uint8_t x, y;
		x = hex_to_int(buf[i]);
		y = hex_to_int(buf[i + 1]);
		result[j] = (x << 4) | y;
	}
}

uint8_t hex_to_int(char c) {
	char const *map = "0123456789abcdef";
	for (int i = 0; i < 16; i++) {
		if (c == map[i]) {
			return i;
		}
	}
	return 0;
}

void print_hex(uint8_t* bytes, unsigned int count) {
	char result[count * 2 + 1];
	for (unsigned int i = 0, j = 0; i < count; i++, j += 2) {
		result[j] = "0123456789abcdef"[bytes[i] >> 4];
		result[j + 1] = "0123456789abcdef"[bytes[i] & 15];
	}
	result[count * 2] = 0;
	Serial.print(result);
}

// WiFi
void printMacAddress() {
	byte mac[6];
	WiFi.macAddress(mac);
	Serial.print("MAC Address: ");
	Serial.print(mac[5],HEX);
	Serial.print(":");
	Serial.print(mac[4],HEX);
	Serial.print(":");
	Serial.print(mac[3],HEX);
	Serial.print(":");
	Serial.print(mac[2],HEX);
	Serial.print(":");
	Serial.print(mac[1],HEX);
	Serial.print(":");
	Serial.println(mac[0],HEX);
}

void print_configuration() {
	if (memcmp((const uint8_t*)config.net.magic, MAGIC, 4)) {
		Serial.println("NOT CONFIGURED");
	} else {
		Serial.println("CONFIGURATION");
		Serial.print("  SMM Magic: ");
		print_hex(config.net.magic, 4);
		Serial.print("\n  SMM Version: ");
		print_version();
		Serial.print("\n  WiFi Network: ");
		Serial.println((char *)config.net.ssid);
		Serial.print("  Pool Server: smm://");
		print_server_address();
		Serial.print("\n  Address: ");
		Serial.print((char *)config.net.address);
		Serial.print("\n  Status: ");
		Serial.println(config.net.status);
		Serial.println();
	}
}

void print_version() {
	char version[8];
	version[0] = '0' + config.net.version[0];
	version[1] = '.';
	version[2] = '0' + config.net.version[1];
	version[3] = '.';
	version[4] = '0' + config.net.version[2];
	version[5] = '-';
	version[6] = config.net.version[3];
	version[7] = 0;
	Serial.print(version);
}

void print_server_address() {
	uint8_t *address = config.net.server;
	for (int i = 0; i < 3; i++) {
		Serial.print(address[i]);
		Serial.print(".");
	}
	Serial.print(address[3]);
	Serial.print(":");
	Serial.print(config.net.port);
}


void print_ip_address(IPAddress addr) {
	Serial.print(addr[0]);
	Serial.print(".");
	Serial.print(addr[1]);
	Serial.print(".");
	Serial.print(addr[2]);
	Serial.print(".");
	Serial.print(addr[3]);
}

void send_ping() {
	message_t message = get_message("HELO");
//	uint8_t buf[] = "ping";
	IPAddress remote = config.net.server;
	udp.beginPacket(remote, config.net.port);
	udp.write(message.bytes, sizeof(message));
	if (! udp.endPacket()) {
		Serial.println("UDP Send error");
	}
}

boolean receive_ping() {
	int packet_size = udp.parsePacket();
	if (packet_size) {
		uint8_t buf[packet_size];
		udp.read(buf, packet_size);
		Serial.print(' ');
		IPAddress remote = udp.remoteIP();
		print_ip_address(remote);
		return true;
	} else {
		return false;
	}
}

message_t get_message(const char *command) {
	message_t m;
	memcpy(m.header.magic, MAGIC, 4);
	memcpy(m.header.version, VERSION, 4);
	m.header.number = millis();
	memcpy(m.header.command, command, 4);
	m.header.size = 0;
	return m;
}
